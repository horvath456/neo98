#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The layout of the port's state, read from the debug information of the
host: every variable with static storage of src/ and every type one of them
reaches, field by field.

A saved state of a machine with Windows programs is written field by field,
by name. Nobody lists the fields by hand: this tool reads them out of DWARF
(llvm-dwarfdump on the objects of src/) and the addresses of the variables
out of the linked host (its debug map), and writes a table the host loads
beside its program file. A new field of a structure is in the table of the
next build and so in the next state. What a table cannot know is what a
pointer means; that is a rule in src/state_rules/*.rules, and a pointer
without a rule fails the build.

    build/analysis-venv/bin/python tools/state_layout.py # table for build/native-app/neo98-host
    build/analysis-venv/bin/python tools/state_layout.py --stats # numbers only
"""
from __future__ import annotations

import argparse
import collections
from functools import lru_cache
import hashlib
import json
import os
import pickle
import re
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DWARFDUMP = os.environ.get('NEO98_DWARFDUMP') or '/opt/homebrew/opt/llvm/bin/llvm-dwarfdump'
CACHE = ROOT / 'build/native-app/state-layout-cache'
RULES = Path(os.environ.get('NEO98_STATE_RULES') or ROOT / 'src/state_rules')

DIE = re.compile(r'^(0x[0-9a-f]+):( +)(DW_TAG_\w+|NULL)')
ATTRIBUTE = re.compile(r'^\s+(DW_AT_\w+)\t\((.*)\)\s*$')


class Die:
    __slots__ = ('offset', 'tag', 'attributes', 'children', 'parent')

    def __init__(self, offset, tag, parent):
        self.offset, self.tag, self.parent = offset, tag, parent
        self.attributes, self.children = {}, []

    def text(self, name):
        value = self.attributes.get(name)
        if value is None:
            return None
        match = re.match(r'^"(.*)"$', value)
        return match.group(1) if match else value

    def number(self, name, default=None):
        value = self.attributes.get(name)
        if value is None:
            return default
        match = re.match(r'^(0x[0-9a-f]+|\d+)', value)
        return int(match.group(1), 0) if match else default

    def reference(self, name):
        value = self.attributes.get(name)
        if value is None:
            return None
        match = re.match(r'^(0x[0-9a-f]+)', value)
        return int(match.group(1), 16) if match else None


def parse_dwarf(obj):
    """The DIE tree of one object, by offset, and its address table."""
    text = subprocess.run([DWARFDUMP, '--debug-info', '--debug-addr', str(obj)], capture_output=True, text=True,
                          errors='replace', check=True).stdout
    dies, stack, root, addresses, current = {}, [], [], [], None
    in_addr = False
    for line in text.split('\n'):
        if line.startswith('.debug_addr contents'):
            in_addr = True
            continue
        if line.startswith('.debug_info contents'):
            in_addr = False
            continue
        if in_addr:
            if line.startswith('0x'):
                addresses.append(int(line.strip(), 16))
            continue
        match = DIE.match(line)
        if match:
            depth = (len(match.group(2)) - 1) // 2
            del stack[depth:]
            if match.group(3) == 'NULL':
                current = None
                continue
            parent = stack[-1] if stack else None
            current = Die(int(match.group(1), 16), match.group(3), parent)
            dies[current.offset] = current
            (parent.children if parent else root).append(current)
            stack.append(current)
            continue
        if current is not None:
            match = ATTRIBUTE.match(line)
            if match:
                current.attributes.setdefault(match.group(1), match.group(2))
    return dies, root, addresses


def symbols(obj):
    """address -> name of the symbols of one object."""
    out = subprocess.run(['nm', '-n', str(obj)], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] not in ('U', 'u'):
            table.setdefault(int(parts[0], 16), parts[2])
    return table


def spelled(dies, offset, depth=0):
    """A type as a template argument spells it."""
    die = dies.get(offset) if offset is not None else None
    if die is None or depth > 6:
        return 'void'
    if die.tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_typedef', 'DW_TAG_restrict_type'):
        return spelled(dies, die.reference('DW_AT_type'), depth + 1)
    if die.tag in ('DW_TAG_pointer_type', 'DW_TAG_reference_type', 'DW_TAG_rvalue_reference_type'):
        return spelled(dies, die.reference('DW_AT_type'), depth + 1) + '*'
    if die.tag == 'DW_TAG_array_type':
        return spelled(dies, die.reference('DW_AT_type'), depth + 1) + '[]'
    if die.tag == 'DW_TAG_subroutine_type':
        return 'function'
    return qualified(die, dies, depth + 1)


def arguments(die, dies, depth):
    """The template arguments of a type. The compiler names an instance by
    the template alone (simple template names); two instances are two types."""
    name = die.text('DW_AT_name') or ''
    if '<' in name:
        return ''
    found = []

    def take(child):
        if child.tag == 'DW_TAG_template_type_parameter':
            found.append(spelled(dies, child.reference('DW_AT_type'), depth + 1))
        elif child.tag == 'DW_TAG_template_value_parameter':
            found.append(str(child.attributes.get('DW_AT_const_value', '?')))
        elif child.tag == 'DW_TAG_GNU_template_parameter_pack':
            for inner in child.children:
                take(inner)

    for child in die.children:
        take(child)
    return '<' + ','.join(found) + '>' if found else ''


def data_symbols(obj):
    """The names of what one object keeps in writable memory."""
    out = subprocess.run(['nm', '-m', str(obj)], capture_output=True, text=True, errors='replace', check=True).stdout
    names = set()
    for line in out.splitlines():
        match = re.match(r'^[0-9a-f]+ \((__DATA,(?:__data|__bss|__common))\) .*? (\S+)$', line)
        if match:
            names.add(match.group(2))
        elif re.match(r'^[0-9a-f]+ \(common\)', line):
            names.add(line.split()[-1])
    return names


def qualified(die, dies=None, depth=0):
    """The name of a type or variable with the scopes around it."""
    parts = []
    node = die
    while node is not None and node.tag != 'DW_TAG_compile_unit':
        if node.tag in ('DW_TAG_namespace', 'DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type',
                        'DW_TAG_enumeration_type', 'DW_TAG_variable', 'DW_TAG_typedef', 'DW_TAG_subprogram',
                        'DW_TAG_base_type', 'DW_TAG_member'):
            name = node.text('DW_AT_name')
            if node.tag == 'DW_TAG_namespace' and name is None:
                name = '(anonymous namespace)'
            if node.tag == 'DW_TAG_subprogram':
                # The body of a member or of an inlined function names itself
                # through its declaration, which stands where it was declared.
                origin, hops = node, 0
                while name is None and dies is not None and hops < 4:
                    reference = origin.reference('DW_AT_specification') or origin.reference('DW_AT_abstract_origin')
                    origin = dies.get(reference) if reference is not None else None
                    if origin is None:
                        break
                    name, hops = origin.text('DW_AT_name'), hops + 1
                    if name is not None:
                        parts.append(name + '()')
                        outer = qualified(origin.parent, dies, depth + 1) if origin.parent is not None else ''
                        if outer:
                            parts.append(outer)
                        return '::'.join(reversed(parts))
                name = (name or '?') + '()'
            if name is not None and dies is not None and node.tag in ('DW_TAG_structure_type', 'DW_TAG_class_type',
                                                                      'DW_TAG_union_type'):
                name += arguments(node, dies, depth)
            parts.append(name if name is not None else '(unnamed)')
        node = node.parent
    return '::'.join(reversed(parts))


class Unit:
    """What one object contributes: its types by offset, resolved lazily, and
    its variables with static storage."""

    def __init__(self, source, obj):
        self.source = source
        self.dies, self.root, self.addresses = parse_dwarf(obj)
        self.symbols = symbols(obj)
        self.types = {}      # die offset -> key in the global type table
        self.variables = []  # (qualified name, symbol, type die offset)
        for die in self.dies.values():
            if die.tag == 'DW_TAG_variable':
                self.variable(die)
        # A static inside an inline function that was inlined everywhere has
        # no place in the debug information, only its declaration inside the
        # function. Its symbol says which one it is.
        described = {symbol for _, symbol, _ in self.variables}
        missing = data_symbols(obj) - described
        if missing:
            for die in self.dies.values():
                if die.tag != 'DW_TAG_variable' or 'DW_AT_location' in die.attributes or not die.text('DW_AT_name'):
                    continue
                function = die.parent
                while function is not None and function.tag != 'DW_TAG_subprogram':
                    function = function.parent
                linkage = None
                while function is not None and linkage is None:
                    linkage = function.text('DW_AT_linkage_name')
                    reference = function.reference('DW_AT_specification') or function.reference('DW_AT_abstract_origin')
                    function = self.dies.get(reference) if reference is not None else None
                kind = die.reference('DW_AT_type')
                if not linkage or kind is None:
                    continue
                name = die.text('DW_AT_name')
                symbol = f'__ZZ{linkage[2:]}E{len(name)}{name}'
                if symbol in missing:
                    missing.discard(symbol)
                    self.variables.append((qualified(die, self.dies), symbol, kind))

    def variable(self, die):
        location = die.attributes.get('DW_AT_location')
        if not location:
            return
        match = re.match(r'^DW_OP_addrx (0x[0-9a-f]+)$', location) or re.match(r'^DW_OP_addr (0x[0-9a-f]+)$', location)
        if not match:
            return
        value = int(match.group(1), 16)
        address = self.addresses[value] if 'addrx' in location else value
        symbol = self.symbols.get(address)
        declaration = die
        if 'DW_AT_specification' in die.attributes:
            declaration = self.dies.get(die.reference('DW_AT_specification'), die)
        elif 'DW_AT_abstract_origin' in die.attributes:
            declaration = self.dies.get(die.reference('DW_AT_abstract_origin'), die)
        kind = declaration.reference('DW_AT_type') or die.reference('DW_AT_type')
        if symbol is None or kind is None:
            return
        self.variables.append((qualified(declaration, self.dies), symbol, kind))


# The typedefs the rules call addresses (type X address), for describe.
ADDRESSES = set()


def record_key(unit, die):
    """The name of a structure in the table."""
    name = qualified(die, unit.dies)
    if '(unnamed)' in name or '(anonymous namespace)' in name or '()' in name:
        # Private to its file: two files may each have one of this name.
        name = f'{name}@{Path(unit.source).name}'
        if '(unnamed)' in name:
            name += f'#{die.number("DW_AT_decl_line", 0)}'
    return name


# The containers of the standard library. Their insides belong to the
# library and change with it, so the table does not describe them as
# structures: it says what they hold, and src/x86_state_fields.cpp writes
# that (the elements of a vector, the pairs of a map, the object behind a
# unique_ptr or shared_ptr, the text of a string) and builds the container
# anew when it loads. The places inside them are checked here, and a
# container whose insides are not as expected is described as the structure
# it is, which then stops the build with its pointers and unions.
CONTAINERS = {'vector': 24, 'map': 24, 'unique_ptr': 8, 'shared_ptr': 16, 'basic_string': 24}


def alignment(table, key, depth=0):
    record = table.get(key, {})
    kind = record.get('kind')
    if depth > 12:
        return 8
    if kind == 'scalar':
        return min(8, max(1, record['size']))
    if kind == 'array':
        return alignment(table, record['of'], depth + 1)
    if kind in ('struct', 'union') and record.get('fields'):
        return max(alignment(table, field['type'], depth + 1) for field in record['fields'])
    return 8 if kind != 'void' else 1


def members(unit, die, depth=0):
    """(name, offset, die of the type) of every data member of a structure,
    through its bases and its unnamed members."""
    found = []
    if depth > 6:
        return found
    for child in die.children:
        if child.tag not in ('DW_TAG_member', 'DW_TAG_inheritance') or child.attributes.get('DW_AT_external') == 'true':
            continue
        at = child.number('DW_AT_data_member_location', 0)
        inner = unit.dies.get(child.reference('DW_AT_type'))
        while inner is not None and inner.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type'):
            inner = unit.dies.get(inner.reference('DW_AT_type'))
        name = child.text('DW_AT_name')
        # A member of the library's own (__layout_ of a vector) is looked into like an unnamed one.
        if child.tag == 'DW_TAG_inheritance' or ((name is None or name in ('__layout_',)) and inner is not None and inner.tag in
                                                 ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type')):
            found += [(n, at + o, d) for n, o, d in members(unit, inner, depth + 1)] if inner is not None else []
        else:
            found.append((name, at, child.reference('DW_AT_type')))
    return found


def container(unit, die, table, depth):
    """The record of a container of the standard library, or None."""
    name = die.text('DW_AT_name') or ''
    scope = die.parent
    while scope is not None and scope.tag == 'DW_TAG_namespace' and scope.text('DW_AT_name') != 'std':
        scope = scope.parent
    if name.split('<')[0] not in CONTAINERS or scope is None or scope.tag != 'DW_TAG_namespace' or \
            die.number('DW_AT_byte_size', 0) != CONTAINERS[name.split('<')[0]]:
        return None
    name = name.split('<')[0]
    parameters = [child.reference('DW_AT_type') for child in die.children if child.tag == 'DW_TAG_template_type_parameter']
    inside = {n: (o, t) for n, o, t in members(unit, die) if n}
    if name == 'basic_string':
        first = unit.dies.get(parameters[0]) if parameters else None
        if first is None or first.number('DW_AT_byte_size', 0) != 1:
            return None
        return 'string', {'kind': 'string', 'size': 24, 'fields': []}
    if not parameters or parameters[0] is None:
        return None
    if name == 'vector':
        if [inside.get(n, (None,))[0] for n in ('__begin_', '__end_', '__capacity_')] != [0, 8, 16]:
            return None
        element = describe(unit, parameters[0], table, depth + 1)
        return f'vector<{element}>', {'kind': 'vector', 'size': 24, 'fields': [{'name': 'element', 'offset': 0, 'type': element}]}
    if name in ('unique_ptr', 'shared_ptr'):
        if inside.get('__ptr_', (None,))[0] != 0 or (name == 'shared_ptr' and inside.get('__cntrl_', (None,))[0] != 8):
            return None
        target = describe(unit, parameters[0], table, depth + 1)
        kind = 'unique' if name == 'unique_ptr' else 'shared'
        return f'{kind}<{target}>', {'kind': kind, 'size': CONTAINERS[name], 'fields': [{'name': 'target', 'offset': 0, 'type': target}]}
    # A map: a red-black tree of nodes, each with its pair behind the links.
    tree = next((unit.dies.get(child.reference('DW_AT_type')) for child in die.children
                 if child.tag == 'DW_TAG_member' and child.text('DW_AT_name') == '__tree_'), None)
    while tree is not None and tree.tag in ('DW_TAG_typedef', 'DW_TAG_const_type'):
        tree = unit.dies.get(tree.reference('DW_AT_type'))
    inside = {n: (o, t) for n, o, t in members(unit, tree) if n} if tree is not None else {}
    if len(parameters) < 2 or parameters[1] is None or \
            [inside.get(n, (None,))[0] for n in ('__begin_node_', '__end_node_', '__size_')] != [0, 8, 16]:
        return None
    # The node of this map: the one whose value is the tree's.
    wanted = next((spelled(unit.dies, child.reference('DW_AT_type')) for child in (tree.children if tree is not None else [])
                   if child.tag == 'DW_TAG_template_type_parameter'), None)
    if not hasattr(unit, 'tree_nodes'):
        unit.tree_nodes = {}
        for candidate in unit.dies.values():
            if candidate.tag in ('DW_TAG_structure_type', 'DW_TAG_class_type') and candidate.children and \
                    (candidate.text('DW_AT_name') or '').split('<')[0] == '__tree_node':
                first = next((child.reference('DW_AT_type') for child in candidate.children
                              if child.tag == 'DW_TAG_template_type_parameter'), None)
                unit.tree_nodes.setdefault(spelled(unit.dies, first), candidate)
    node = unit.tree_nodes.get(wanted)
    if node is None or not node.children:
        return None
    links = {n: o for n, o, _ in members(unit, node) if n}
    values = [o for n, o, _ in members(unit, node) if n not in ('__left_', '__right_', '__parent_', '__is_black_')]
    if [links.get(n) for n in ('__left_', '__right_', '__parent_', '__is_black_')] != [0, 8, 16, 24] or not values:
        return None
    key = describe(unit, parameters[0], table, depth + 1)
    mapped = describe(unit, parameters[1], table, depth + 1)
    table.setdefault('void', {'kind': 'void', 'size': 0})
    value = min(values)
    align = alignment(table, mapped)
    second = (table[key]['size'] + align - 1) // align * align
    if value + second + table[mapped]['size'] > node.number('DW_AT_byte_size', 0):
        return None
    return f'map<{key},{mapped}>', {'kind': 'map', 'size': 24, 'fields': [
        {'name': 'key', 'offset': value, 'type': key}, {'name': 'mapped', 'offset': value + second, 'type': mapped},
        {'name': 'node', 'offset': node.number('DW_AT_byte_size', 0), 'type': 'void'}]}


@lru_cache(maxsize=256)
def declaration_lines(source):
    try:
        return Path(source).read_text().splitlines(keepends=True)
    except (OSError, UnicodeError):
        return []


def address_typedef(die):
    """Recognize integer host addresses before DWARF erases their typedef.

    Only stored/reachable types need a rule. Local arithmetic aliases are
    harmless. Ordinary 64-bit counters remain ordinary integers.
    """
    name = die.text('DW_AT_name')
    source = die.text('DW_AT_decl_file')
    line = die.number('DW_AT_decl_line', 0)
    if not name or not source or not line:
        return False
    lines = declaration_lines(source)
    # A declaration can share a line with other declarations, or span lines.
    text = ''.join(lines[line - 1:line + 15])
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    alias = re.escape(name)
    declaration = re.search(r'\busing\s+' + alias + r'\s*=\s*([^;]+);'
                            r'|\btypedef\s+([^;]+?)\s+' + alias + r'\s*;', text)
    return bool(declaration and (declaration[1] or declaration[2]).strip() == '__UINTPTR_TYPE__')


def address_problems(types, reached, rules=None):
    return [f'type {key}: integer host address needs a type {key} address rule'
            for key in sorted(reached) if types[key].get('unruled_address')
            and (rules or {}).get('type', {}).get(key, ('none',))[0] not in ('host', 'skip')]


def describe(unit, offset, table, depth=0):
    """The key of a type in the table, describing it on the way. A type is
    named by what it is, so two objects that know the same type agree."""
    if offset in unit.types:
        return unit.types[offset]
    die = unit.dies[offset]
    tag = die.tag
    if tag == 'DW_TAG_typedef' and qualified(die, unit.dies) in ADDRESSES:
        table.setdefault('address', {'kind': 'address', 'size': 8})
        unit.types[offset] = 'address'
        return 'address'
    if tag == 'DW_TAG_typedef' and address_typedef(die):
        key = qualified(die, unit.dies)
        underlying = describe(unit, die.reference('DW_AT_type'), table, depth + 1)
        table[key] = dict(table[underlying], unruled_address=True)
        unit.types[offset] = key
        return key
    if tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_typedef', 'DW_TAG_restrict_type',
               'DW_TAG_atomic_type'):
        inner = die.reference('DW_AT_type')
        if inner is None:  # const void
            key = 'void'
            table.setdefault(key, {'kind': 'void', 'size': 0})
        else:
            key = describe(unit, inner, table, depth + 1)
        unit.types[offset] = key
        return key
    if tag == 'DW_TAG_base_type':
        size = die.number('DW_AT_byte_size', 0)
        encoding = die.text('DW_AT_encoding') or ''
        kind = 'float' if 'float' in encoding else 'bool' if 'boolean' in encoding else 'int'
        key = f'{kind}{size}' + ('s' if 'signed' in encoding and 'unsigned' not in encoding else '')
        table.setdefault(key, {'kind': 'scalar', 'size': size, 'name': die.text('DW_AT_name')})
    elif tag == 'DW_TAG_enumeration_type':
        size = die.number('DW_AT_byte_size', 4)
        key = f'int{size}'
        table.setdefault(key, {'kind': 'scalar', 'size': size, 'name': 'enum'})
    elif tag in ('DW_TAG_pointer_type', 'DW_TAG_reference_type', 'DW_TAG_rvalue_reference_type'):
        inner = die.reference('DW_AT_type')
        target = unit.dies.get(inner) if inner is not None else None
        while target is not None and target.tag in ('DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_typedef'):
            inner = target.reference('DW_AT_type')
            target = unit.dies.get(inner) if inner is not None else None
        if target is not None and target.tag == 'DW_TAG_subroutine_type':
            key = 'function*'
            table.setdefault(key, {'kind': 'function', 'size': 8})
        else:
            # A structure may point at itself: the pointer has its name
            # before the structure is described.
            if target is not None and target.tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type'):
                unit.types[offset] = record_key(unit, target) + '*'
            pointee = describe(unit, inner, table, depth + 1) if inner is not None and target is not None else 'void'
            table.setdefault('void', {'kind': 'void', 'size': 0})
            key = pointee + '*'
            table.setdefault(key, {'kind': 'pointer', 'size': 8, 'to': pointee})
    elif tag == 'DW_TAG_ptr_to_member_type':
        key = 'member*'
        table.setdefault(key, {'kind': 'function', 'size': die.number('DW_AT_byte_size', 16)})
    elif tag == 'DW_TAG_subroutine_type':
        key = 'function'
        table.setdefault(key, {'kind': 'void', 'size': 0})
    elif tag == 'DW_TAG_array_type':
        element = describe(unit, die.reference('DW_AT_type'), table, depth + 1)
        counts = []
        for child in die.children:
            if child.tag == 'DW_TAG_subrange_type':
                count = child.number('DW_AT_count')
                if count is None:
                    upper = child.number('DW_AT_upper_bound')
                    count = upper + 1 if upper is not None else 0
                counts.append(count)
        key = element
        for count in reversed(counts or [0]):
            inner = key
            key = f'{inner}[{count}]'
            table.setdefault(key, {'kind': 'array', 'of': inner, 'count': count,
                                   'size': table[inner]['size'] * count})
    elif tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type'):
        made = container(unit, die, table, depth) if depth < 40 else None
        if made:
            key, record = made
            unit.types[offset] = key
            table.setdefault(key, record)
            return key
        key = record_key(unit, die)
        unit.types[offset] = key
        if die.attributes.get('DW_AT_declaration') == 'true' and not die.children:
            table.setdefault(key, {'kind': 'opaque', 'size': 0})
            return key
        known = table.get(key)
        if known and known['kind'] != 'opaque':
            return key
        record = {'kind': 'union' if tag == 'DW_TAG_union_type' else 'struct', 'size': die.number('DW_AT_byte_size', 0),
                  'fields': [], 'source': Path(unit.source).name}
        table[key] = record
        for child in die.children:
            if child.tag == 'DW_TAG_inheritance':
                base = describe(unit, child.reference('DW_AT_type'), table, depth + 1)
                record['fields'].append({'name': '(base ' + base + ')', 'offset': child.number('DW_AT_data_member_location', 0),
                                         'type': base})
            elif child.tag == 'DW_TAG_member':
                if child.attributes.get('DW_AT_external') == 'true' or 'DW_AT_data_member_location' not in child.attributes \
                        and 'DW_AT_data_bit_offset' not in child.attributes and tag != 'DW_TAG_union_type':
                    continue  # a static member is a variable of its own
                member = describe(unit, child.reference('DW_AT_type'), table, depth + 1)
                field = {'name': child.text('DW_AT_name') or '(unnamed)', 'offset': child.number('DW_AT_data_member_location', 0),
                         'type': member}
                if 'DW_AT_bit_size' in child.attributes:
                    field['bits'] = (child.number('DW_AT_data_bit_offset', 0), child.number('DW_AT_bit_size', 0))
                if (child.text('DW_AT_name') or '').startswith('_vptr$'):
                    field['type'] = 'vtable*'
                    table.setdefault('vtable*', {'kind': 'vtable', 'size': 8})
                record['fields'].append(field)
        return key
    elif tag == 'DW_TAG_unspecified_type':
        key = 'nullptr'
        table.setdefault(key, {'kind': 'scalar', 'size': 8, 'name': 'nullptr_t'})
    else:
        key = f'unknown:{tag}'
        table.setdefault(key, {'kind': 'opaque', 'size': 0})
    unit.types[offset] = key
    return key


def source_objects(binary):
    """source path -> object, for the objects of src/ this program was linked
    from (its debug map names them). A source of the same name outside src/
    stands for the one in src/: the update test builds a host from a changed
    copy of a file (tools/test_state_host.py)."""
    cache = Path(os.environ.get('NEO98_STATE_OBJECT_CACHE') or ROOT / 'build/native-app/object-cache-fast')
    result = {}
    directory, source = None, None
    for line in subprocess.run(['nm', '-a', '-p', str(binary)], capture_output=True, text=True, errors='replace', check=True).stdout.splitlines():
        parts = line.split(maxsplit=5)
        if len(parts) >= 5 and parts[4] == 'SO':
            if len(parts) == 5:
                directory, source = None, None
            elif parts[5].endswith('/'):
                directory = Path(parts[5])
            else:
                source = Path(parts[5])
                if not source.is_absolute() and directory:
                    source = directory / source
        elif len(parts) == 6 and parts[4] == 'OSO' and source and \
                (ROOT / 'src' / source.name).exists() and \
                (source.parent == ROOT / 'src' or ROOT / 'build' in source.parents):
            obj = Path(parts[5])
            if not obj.exists():
                obj = cache / 'objects' / obj.name
            if not obj.exists():
                raise SystemExit(f'state layout: the linked object is missing: {parts[5]}')
            result[source] = obj
    return result


def load_unit(item):
    source, obj = item
    CACHE.mkdir(parents=True, exist_ok=True)
    stamp = hashlib.sha256((obj.name + str(Path(__file__).stat().st_mtime_ns) + ','.join(sorted(ADDRESSES))).encode()).hexdigest()[:24]
    cached = CACHE / f'{stamp}.pickle'
    if cached.exists():
        return pickle.loads(cached.read_bytes())
    unit = Unit(str(source), obj)
    table = {}
    variables = [(name, symbol, describe(unit, kind, table)) for name, symbol, kind in unit.variables]
    # Every structure the file knows whole: another file may reach it through
    # a pointer and only know its name.
    for die in list(unit.dies.values()):
        if die.tag in ('DW_TAG_structure_type', 'DW_TAG_class_type', 'DW_TAG_union_type') and die.children and \
                die.attributes.get('DW_AT_declaration') != 'true' and die.text('DW_AT_name'):
            describe(unit, die.offset, table)
    # What the object keeps in writable memory without the debug information
    # saying what it is: nothing may be state behind the table's back.
    described = {symbol for _, symbol, _ in unit.variables}
    unknown = sorted(name for name in data_symbols(obj) - described
                     if not name.startswith(('__ZGV', 'l_', 'ltmp', 'L', '__ZTV', '__ZTI', '__ZTS',
                                             # what the sanitizers add to an object (neo98-host-asan)
                                             '____asan_', '___asan_', '___odr_asan_gen_', '___ubsan_')))
    result = (str(source), obj.name, variables, table, unknown)
    cached.write_bytes(pickle.dumps(result))
    return result


def linked_addresses(binary):
    """(object file name, symbol) -> (address, section) of the statics of the
    linked host, from its debug map, and the globals by name alone."""
    out = subprocess.run(['nm', '-a', '-m', str(binary)], capture_output=True, text=True, errors='replace', check=True).stdout
    plain = subprocess.run(['nm', '-m', str(binary)], capture_output=True, text=True, errors='replace', check=True).stdout
    section_of = {}
    for line in plain.splitlines():
        match = re.match(r'^([0-9a-f]{16}) \(([^)]*)\) .*? (\S+)$', line)
        if match:
            section_of[(int(match.group(1), 16), match.group(3))] = match.group(2)
    by_object, by_name, current, guards = {}, {}, None, {}
    for line in subprocess.run(['nm', '-a', '-p', str(binary)], capture_output=True, text=True, errors='replace', check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[4] == 'OSO':
            current = Path(parts[5]).name
        elif len(parts) >= 6 and parts[4] == 'STSYM' and current:
            by_object[(current, parts[5])] = int(parts[0], 16)
        elif len(parts) == 3 and parts[1] in 'DdBbSs':
            by_name.setdefault(parts[2], int(parts[0], 16))
            if parts[2].startswith('__ZGV'):
                guards.setdefault(parts[2], []).append(int(parts[0], 16))
    return by_object, by_name, section_of, guards


def collect(binary):
    ADDRESSES.update(name for name, rule in read_rules()['type'].items() if rule[0] == 'address')
    objects = source_objects(binary)
    # DWARF readers also need the build's memory budget on small hosts.
    jobs = max(1, int(os.environ.get('NEO98_NATIVE_JOBS', '6')))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        units = list(pool.map(load_unit, sorted(objects.items())))
    by_object, by_name, section_of, guards = linked_addresses(binary)
    types, variables, lost = {}, [], []
    unknown, shared = [], set()
    for source, obj, unit_variables, table, unit_unknown in units:
        unknown += [(Path(source).name, name) for name in unit_unknown]
        for key, record in table.items():
            known = types.get(key)
            if known is None or (known['kind'] == 'opaque' and record['kind'] != 'opaque'):
                types[key] = record
        seen = set()
        for name, symbol, kind in unit_variables:
            if symbol in seen:
                continue
            seen.add(symbol)
            address = by_object.get((obj, symbol))
            if address is None:
                address = by_name.get(symbol)
            # An inline function's static is the same in every file that has it.
            if address in shared:
                continue
            if address is None:
                lost.append((source, name, symbol))
                continue
            section = section_of.get((address, symbol), '?')
            # The guard of a static that is made on first use: a load sets it
            # with the value, or the first use would make the static anew.
            guard = guards.get('__ZGV' + symbol[3:], [])
            if (obj, symbol) not in by_object:
                shared.add(address)
            variables.append({'name': name, 'symbol': symbol, 'source': Path(source).name, 'address': address,
                              'type': kind, 'section': section, 'guard': guard[0] if len(guard) == 1 else 0,
                              'guards': len(guard)})
    collect.unknown = unknown
    return types, variables, lost


def reach(types, start):
    """Every type a variable of this type holds, pointers included."""
    seen, queue = set(), [start]
    while queue:
        key = queue.pop()
        if key in seen or key not in types:
            continue
        seen.add(key)
        record = types[key]
        if record['kind'] == 'array':
            queue.append(record['of'])
        elif record['kind'] == 'pointer':
            queue.append(record['to'])
        elif record['kind'] in ('struct', 'union'):
            queue.extend(field['type'] for field in record['fields'])
    return seen


# ---- Rules -----------------------------------------------------------------------
# src/state_rules/*.rules, one rule a line:
#
# field Type::name RULE [argument] # why
# var Name RULE [argument] # why (the qualified name; @file.cpp picks one of two)
# type Type RULE # why
# symbol _mangled RULE [argument] # why (a static the debug information does not describe:
# one inside an inline function that was inlined everywhere)
#
# RULE is one of
# host of the host process: never written; a load keeps what the loading process has
# skip not part of the machine (a cache, a scratch value): never written, zero after a load
# auto a pointer whose target the table cannot type (void *): written when it leads to an
# object of the state, a symbol of the program or nothing; anything else refuses the save
# count an owned array whose length is the field named in the argument (elements)
# bytes owned bytes whose length is the field named in the argument
# cstring an owned string, or one of the program's
# tail an owned block: one structure with bytes behind it to the end of the block
# custom written and loaded by the code named in the argument (src/x86_state_machine.cpp)
# as a pointer the table cannot type that points at the type named in the argument
# header a pointer into an owned block of bytes, behind a head of as many bytes as the argument says
# address (type, on a typedef) a number that holds an address of the host: written as what it
# points at; a number below 4 GiB stays a number
# bind a pointer to an object of the host that the variable named in the argument holds in
# every process (the rasterizer): never written, set from that variable after a load
# tail (type) every block of this type is one structure with bytes behind it, never an array
# of them. A structure that ends in an array of one element (bytes[1]) is one without a rule
RULE_CODES = {'none': 0, 'host': 1, 'skip': 2, 'auto': 3, 'count': 4, 'bytes': 5, 'cstring': 6, 'tail': 7, 'custom': 8, 'as': 9, 'header': 10, 'address': 11, 'bind': 12, 'end': 13}


def read_rules():
    rules = {'field': {}, 'var': {}, 'type': {}, 'symbol': {}}
    for path in sorted(RULES.glob('*.rules')):
        if path.name.endswith('.linux.rules') and not sys.platform.startswith('linux'):
            continue
        if path.name.endswith('.macos.rules') and sys.platform != 'darwin':
            continue
        for number, line in enumerate(path.read_text().splitlines(), 1):
            text = line.split('#', 1)[0].strip()
            if not text:
                continue
            # A name may hold blanks ("(anonymous namespace)"): the rule is the
            # last word, or the one before an argument.
            match = re.match(r'^(field|var|type|symbol)\s+(.+?)\s+(' + '|'.join(RULE_CODES) + r')(?:\s+(.+))?$', text)
            if not match:
                raise SystemExit(f'{path}:{number}: not a rule: {line}')
            if '#' not in line:
                raise SystemExit(f'{path}:{number}: a rule says why (# ...): {line}')
            rules[match.group(1)][match.group(2)] = (match.group(3), match.group(4) or '', f'{path.name}:{number}')
    return rules


def element(types, key):
    while types.get(key, {}).get('kind') == 'array':
        key = types[key]['of']
    return key


def sanitizer_global_sizes(binary):
    """ASan's exact sizes of globals, excluding its poisoned red zones.

    Mach-O has no symbol sizes. In an ordinary link the next symbol checks
    an explicitly typed static; in an ASan link that distance includes red
    zones and unrelated instrumentation. Use the __asan_global records
    instead, with the pointer format declared by dyld's chained fixups.
    """
    data = Path(binary).read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        return {}
    sections, base, fixups = [], None, None
    offset = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, offset)
        if command == 0x19:  # LC_SEGMENT_64
            vmaddr, _, fileoff, filesize = struct.unpack_from('<4Q', data, offset + 24)
            if fileoff == 0 and filesize:
                base = vmaddr
            for i in range(struct.unpack_from('<I', data, offset + 64)[0]):
                section = offset + 72 + i * 80
                if data[section:section + 16].split(b'\0')[0] == b'__asan_globals':
                    sections.append(struct.unpack_from('<QQI', data, section + 32))
        elif command == 0x80000034:  # LC_DYLD_CHAINED_FIXUPS
            fixups = struct.unpack_from('<I', data, offset + 8)[0]
        offset += size
    if not sections:
        return {}
    formats = set()
    if fixups is not None:
        starts = fixups + struct.unpack_from('<I', data, fixups + 4)[0]
        for i in range(struct.unpack_from('<I', data, starts)[0]):
            relative = struct.unpack_from('<I', data, starts + 4 + i * 4)[0]
            if relative:
                formats.add(struct.unpack_from('<H', data, starts + relative + 6)[0])
        if formats not in ({2}, {6}):
            raise SystemExit(f'state layout: unsupported ASan global fixup formats: {sorted(formats)}')
    result = {}
    for _, size, fileoff in sections:
        if size % 64:
            raise SystemExit('state layout: incomplete ASan global record')
        for offset in range(fileoff, fileoff + size, 64):
            address, actual = struct.unpack_from('<QQ', data, offset)
            if fixups is not None:
                if address >> 63:
                    raise SystemExit('state layout: an ASan global address is an import')
                target = (address & ((1 << 36) - 1)) | (((address >> 36) & 255) << 56)
                address = target + base if formats == {6} else target
            if address:
                result[address] = actual
    return result


def check_and_write(binary, output, types, variables, rules, quiet=False):
    """The table of the host, and the list of what it cannot decide alone."""
    writable = [v for v in variables if v['section'].startswith('__DATA,') and 'const' not in v['section']
                and 'thread' not in v['section']]
    problems, used_rules = [], set()
    used_rules.update(('type', name) for name in ADDRESSES if 'address' in types)

    def rule_of(kind, name, source=None):
        table = rules[kind]
        for candidate in ([f'{name}@{source}'] if source else []) + [name]:
            if candidate in table:
                used_rules.add((kind, candidate))
                return table[candidate]
        return ('none', '', '')

    # What the debug information does not describe is described by a rule, or stops the build.
    by_name = linked_addresses(binary)[1]
    global_sizes = sanitizer_global_sizes(binary)
    # ASan leaves coalesced inline statics uninstrumented. Their next
    # neighbour in the linked image may be far away; the original object's
    # local labels still delimit their storage before linker coalescing.
    object_sizes = {}
    if global_sizes:
        needed = {name for name, rule in rules['symbol'].items() if rule[0] == 'as' and
                  by_name.get(name) not in global_sizes}
        for obj in source_objects(binary).values():
            if not needed:
                break
            output_symbols = subprocess.run(['nm', '-n', '-m', str(obj)], capture_output=True, text=True,
                                            check=True).stdout
            sections = collections.defaultdict(list)
            for line in output_symbols.splitlines():
                match = re.match(r'^([0-9a-f]{16}) \(([^)]*)\) .*? (\S+)$', line)
                if match:
                    sections[match[2]].append((int(match[1], 16), match[3]))
            for symbols in sections.values():
                for i, (address, name) in enumerate(symbols):
                    if name not in needed:
                        continue
                    after = next((other for other, _ in symbols[i + 1:] if other > address), None)
                    if after is not None:
                        object_sizes[name] = after - address
                        needed.remove(name)
    order = sorted(set(by_name.values()))
    told = set()
    for source, name in getattr(collect, 'unknown', []):
        rule = rules['symbol'].get(name)
        if rule is None or name not in by_name:
            problems.append(f'{source}: {name} lies in writable memory and the debug information does not describe it')
            continue
        used_rules.add(('symbol', name))
        if name in told:
            continue
        told.add(name)
        key = rule[1] if rule[0] == 'as' else 'void'
        if rule[0] not in ('as', 'host', 'skip'):
            problems.append(f'{rule[2]}: a symbol is given a type (as), or to the host (host), or left out (skip)')
            continue
        while key not in types and re.search(r'(\*|\[\d+\])$', key):
            # A pointer to or an array of a type the table has.
            pending, inner = [], key
            while inner not in types and re.search(r'(\*|\[\d+\])$', inner):
                suffix = re.search(r'(\*|\[\d+\])$', inner).group(1)
                pending.append(suffix)
                inner = inner[:-len(suffix)]
            if inner not in types:
                break
            for suffix in reversed(pending):
                outer = inner + suffix
                if suffix == '*':
                    types.setdefault(outer, {'kind': 'pointer', 'size': 8, 'to': inner})
                else:
                    count = int(suffix[1:-1])
                    types.setdefault(outer, {'kind': 'array', 'of': inner, 'count': count, 'size': types[inner]['size'] * count})
                inner = outer
        if key not in types:
            problems.append(f'{rule[2]}: the rule names a type the table does not have: {key}')
            continue
        address = by_name[name]
        after = order[order.index(address) + 1] if order.index(address) + 1 < len(order) else address
        size = types[key]['size']
        actual = global_sizes.get(address)
        extent = object_sizes.get(name, after - address)
        fits = actual == size if actual is not None else size <= extent < size + 64
        if rule[0] == 'as' and not fits:
            problems.append(f'{rule[2]}: {name} has {actual if actual is not None else extent} bytes in the program and {key} has {size}')
            continue
        # The names keep the leading underscore of Mach-O on every platform;
        # c++filt of macOS drops it by itself, GNU c++filt when asked to.
        demangle = ['c++filt'] if sys.platform == 'darwin' else ['c++filt', '--strip-underscore']
        readable = subprocess.run([*demangle, name], capture_output=True, text=True, check=True).stdout.strip() or name
        writable.append({'name': readable, 'symbol': name, 'source': '', 'address': address, 'type': key,
                         'section': '__DATA,__data', 'guard': 0, 'guards': 0,
                         'fixed': ('none', '', '') if rule[0] == 'as' else (rule[0], '', rule[2])})
    places = {}
    for variable in writable:
        other = places.setdefault(variable['address'], variable)
        if other is not variable and other['symbol'] != variable['symbol']:
            problems.append(f'var {variable["name"]}@{variable["source"]}: at the address of {other["name"]}@{other["source"]}')
    reached = set()
    for variable in writable:
        variable['rule'] = variable.get('fixed') or rule_of('var', variable['name'], variable['source'])
        if variable['rule'][0] in ('host', 'skip', 'custom'):
            continue
        if variable['guards'] > 1:
            problems.append(f'var {variable["name"]}@{variable["source"]}: its guard has a name two files use')
        reached |= reach_ruled(types, variable['type'], rules, used_rules)
        inner = types.get(element(types, variable['type']), {})
        if variable['rule'][0] == 'none':
            if inner.get('kind') == 'pointer' and needs_rule(types, inner['to'], rules, used_rules):
                problems.append(f'var {variable["name"]}@{variable["source"]}: a pointer to {inner["to"]}, which the table cannot type')
            if inner.get('kind') == 'union' and has_pointer(types, element(types, variable['type'])):
                problems.append(f'var {variable["name"]}@{variable["source"]}: a union with a pointer in it')
    problems.extend(address_problems(types, reached, rules))
    for key in sorted(reached):
        record = types[key]
        if record['kind'] not in ('struct', 'union'):
            continue
        for field in record['fields']:
            if field['type'] not in types:
                problems.append(f'field {key}::{field["name"]}: of a type the table does not have ({field["type"]})')
            field['rule'] = rule_of('field', f'{key}::{field["name"]}')
            if field['rule'][0] == 'as' and types.get(field['rule'][1], {}).get('kind') != 'struct':
                problems.append(f'field {key}::{field["name"]}: the rule names a type the table does not have: {field["rule"][1]}')
            if field['rule'][0] == 'bind' and not any(v['name'] == field['rule'][1] for v in variables):
                problems.append(f'field {key}::{field["name"]}: the rule binds it to a variable the table does not have: {field["rule"][1]}')
            if field['rule'][0] in ('count', 'bytes', 'tail'):
                target = types.get(field['type'], {})
                owner = record
                if field['rule'][0] == 'tail':
                    owner = types.get(target.get('to'), {})
                argument = field['rule'][1]
                if argument:
                    sibling = next((f for f in owner.get('fields', []) if f['name'] == argument), None)
                    number = types.get(sibling['type'], {}) if sibling else {}
                    if number.get('kind') != 'scalar' or not 0 < number.get('size', 0) <= 8:
                        problems.append(f'field {key}::{field["name"]}: the rule names a field that is not a number: {argument}')
            if field['rule'][0] != 'none':
                continue
            inner = types.get(element(types, field['type']), {})
            if inner.get('kind') == 'pointer' and needs_rule(types, inner['to'], rules, used_rules):
                problems.append(f'field {key}::{field["name"]}: a pointer to {inner["to"]}, which the table cannot type')
            if inner.get('kind') == 'union' and has_pointer(types, element(types, field['type'])):
                problems.append(f'field {key}::{field["name"]}: a union with a pointer in it')
            if record['kind'] == 'union' and inner.get('kind') in ('pointer', 'function', 'vtable'):
                problems.append(f'field {key}::{field["name"]}: a pointer in a union')
    for kind in rules:
        for name, (_, _, where) in rules[kind].items():
            if (kind, name) not in used_rules:
                problems.append(f'{where}: the rule for {kind} {name} matches nothing any more')
    # ---- the file ----
    strings, string_at = bytearray(b'\0'), {'': 0}

    def text(value):
        value = value or ''
        if value not in string_at:
            string_at[value] = len(strings)
            strings.extend(value.encode('utf-8', 'replace') + b'\0')
        return string_at[value]

    order = sorted(reached | {'void'})
    types.setdefault('void', {'kind': 'void', 'size': 0})
    index = {key: i for i, key in enumerate(order)}
    kinds = {'scalar': 0, 'pointer': 2, 'function': 3, 'array': 4, 'struct': 5, 'union': 6, 'opaque': 7, 'void': 8,
             'vtable': 9, 'address': 10, 'vector': 11, 'unique': 12, 'shared': 13, 'map': 14, 'string': 15}
    type_records, field_records = [], []
    for key in order:
        record = types[key]
        a = b = 0
        if record['kind'] == 'scalar':
            a = (1 if key.startswith('float') else 0) | (2 if key.endswith('s') else 0) | (4 if key.startswith('bool') else 0)
        elif record['kind'] == 'pointer':
            a = index.get(record['to'], index['void'])
        elif record['kind'] == 'array':
            a, b = index[record['of']], record['count']
        begin = len(field_records)
        if 'fields' in record:
            for field in record['fields']:
                rule = field.get('rule', ('none', '', ''))
                bits = field.get('bits', (0, 0))
                field_records.append(struct.pack('<IIIHHII', text(field['name']), index.get(field['type'], index['void']),
                                                 field['offset'], bits[0] & 0xffff, bits[1], RULE_CODES[rule[0]], text(rule[1])))
        type_rule = rule_of('type', key)
        # A structure that ends in an array of one element or none is the
        # head of a block with bytes behind it (bytes[1]): one structure, not
        # an array of them, whatever the size of its block.
        if type_rule[0] == 'none' and record['kind'] == 'struct' and record['fields']:
            last = types.get(record['fields'][-1]['type'], {})
            if last.get('kind') == 'array' and last.get('count', 2) <= 1 and \
                    record['fields'][-1]['offset'] + max(last.get('size', 0), 1) >= record['size'] - 7:
                type_rule = ('tail', '', '')
        type_records.append(struct.pack('<IIIIIIII', text(key), kinds[record['kind']], record['size'], a, b, begin,
                                        len(field_records) - begin, RULE_CODES[type_rule[0]]))
    variable_records = []
    for variable in sorted(writable, key=lambda v: (v['source'], v['name'], v['symbol'])):
        if variable['type'] not in index:
            index_type = index['void']
        else:
            index_type = index[variable['type']]
        variable_records.append(struct.pack('<IIIIQIIQ', text(variable['name']), text(variable['source']), index_type,
                                            RULE_CODES[variable['rule'][0]], variable['address'], text(variable['rule'][1]),
                                            text(variable['symbol']), variable['guard']))
    # How far a symbol of data reaches, where the debug information says: a
    # pointer behind its end names nothing (an unnamed constant).
    sized = {v['address']: types.get(v['type'], {}).get('size', 0) for v in variables}
    if sys.platform.startswith('linux'):
        sized = {**sanitizer_global_sizes(binary), **sized}
    symbol_records = [struct.pack('<QIIII', address, text(name), text(obj), sized.get(address, 0), 0)
                      for address, name, obj in sorted(program_symbols(binary))]
    header = struct.pack('<8sIIIII', b'A98LAY04', len(type_records), len(field_records), len(variable_records),
                         len(symbol_records), len(strings))
    if sys.platform.startswith('linux'):
        uuid = build_identity(binary)
    else:
        identity = subprocess.run(['dwarfdump', '--uuid', str(binary)], capture_output=True, text=True, check=True).stdout.split()
        uuid = bytes.fromhex(identity[1].replace('-', '')) if len(identity) > 1 else b'\0' * 16
    if output:
        Path(output).write_bytes(header + uuid + b'\0\0\0\0' + b''.join(type_records) + b''.join(field_records) +
                                 b''.join(variable_records) + b''.join(symbol_records) + bytes(strings))
    if not quiet:
        fields = sum(len(types[key]['fields']) for key in reached if types[key]['kind'] in ('struct', 'union'))
        print(f'state layout: {len(variable_records)} variables, {len(order)} types, {fields} fields, '
              f'{len(symbol_records)} symbols, {len(problems)} without a rule')
    return problems


def has_pointer(types, key, seen=None):
    seen = seen or set()
    if key in seen or key not in types:
        return False
    seen.add(key)
    record = types[key]
    if record['kind'] in ('pointer', 'function', 'vtable', 'address', 'vector', 'unique', 'shared', 'map', 'string'):
        return True
    if record['kind'] == 'array':
        return has_pointer(types, record['of'], seen)
    if record['kind'] in ('struct', 'union'):
        return any(has_pointer(types, field['type'], seen) for field in record['fields'])
    return False


def needs_rule(types, pointee, rules, used):
    """Whether a pointer to this type cannot be followed by the table: its
    target has no fields the table knows."""
    kind = types.get(pointee, {}).get('kind', 'opaque')
    if pointee in rules['type']:
        used.add(('type', pointee))
        return False
    return kind in ('opaque', 'void')


def reach_ruled(types, start, rules, used):
    """Every type a value of this type holds or owns, stopping at what a rule
    takes out of the state."""
    seen, queue = set(), [start]
    while queue:
        key = queue.pop()
        if key in seen or key not in types:
            continue
        seen.add(key)
        record = types[key]
        if key in rules['type'] and rules['type'][key][0] in ('host', 'skip'):
            used.add(('type', key))
            continue
        if record['kind'] == 'array':
            queue.append(record['of'])
        elif record['kind'] == 'pointer':
            queue.append(record['to'])
        elif 'fields' in record:
            for field in record['fields']:
                rule = rules['field'].get(f'{key}::{field["name"]}')
                if rule:
                    used.add(('field', f'{key}::{field["name"]}'))
                    if rule[0] in ('host', 'skip', 'custom', 'bind'):
                        continue
                    if rule[0] == 'as':
                        queue.append(rule[1])
                queue.append(field['type'])
    return seen


def object_names():
    """file name of an object in the cache -> its source, which stays the same from build to build."""
    cache = Path(os.environ.get('NEO98_STATE_OBJECT_CACHE') or ROOT / 'build/native-app/object-cache-fast')
    names = {}
    for entry in (cache / 'entries').glob('*.json'):
        record = json.loads(entry.read_text())
        names[record['object_sha256'] + '.o'] = Path(record['source']).name
    return names


def program_symbols(binary):
    """(address, name, object) of every symbol of the host a pointer of the
    state may name: functions and data. A static keeps the source of its
    object, because two files may use one name."""
    names = object_names()
    result, current, seen = [], '', set()
    lines = subprocess.run(['nm', '-a', '-p', str(binary)], capture_output=True, text=True, errors='replace', check=True).stdout.splitlines()
    for line in lines:
        parts = line.split()
        if len(parts) >= 6 and parts[4] == 'OSO':
            leaf = Path(parts[5]).name
            current = names.get(leaf, 'host' if leaf.startswith('.') else leaf)
        elif len(parts) >= 6 and parts[4] in ('FUN', 'STSYM') and parts[5] and int(parts[0], 16):
            address = int(parts[0], 16)
            result.append((address, parts[5], current))
            seen.add(address)
    counts = collections.Counter()
    plain = []
    for line in lines:
        parts = line.split()
        if len(parts) == 3 and parts[1] in 'TtDdBbSs' and not parts[2].startswith('ltmp'):
            plain.append((int(parts[0], 16), parts[2]))
            counts[parts[2]] += 1
    for address, name in plain:
        # A name two objects without debug information share names nothing.
        if address not in seen and counts[name] == 1:
            result.append((address, name, ''))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--stats', action='store_true')
    parser.add_argument('--show', help='print the types and variables whose name holds this text, and stop')
    parser.add_argument('--problems', type=Path, help='write the list of what needs a rule here instead of failing')
    arguments = parser.parse_args()
    output = arguments.output or Path(str(arguments.binary) + '.layout')
    types, variables, lost = collect(arguments.binary)
    if arguments.show:
        for key, record in sorted(types.items()):
            if arguments.show in key:
                print(f'type {key}: {record["kind"]} {record["size"]} bytes')
                for field in record.get('fields', []):
                    print(f'    +{field["offset"]:<6} {field["name"]}: {field["type"]}')
        for variable in variables:
            if arguments.show in variable['name']:
                print(f'var {variable["name"]}@{variable["source"]}: {variable["type"]} at {variable["address"]:x} {variable["section"]}')
        return 0
    rules = read_rules()
    problems = check_and_write(arguments.binary, output, types, variables, rules)
    if arguments.problems:
        arguments.problems.write_text('\n'.join(problems) + '\n')
        return 0
    for problem in problems[:60]:
        print('state layout:', problem)
    return 1 if problems else 0


if sys.platform.startswith('linux'):
    from state_layout_elf import install
    install(sys.modules[__name__])

if __name__ == '__main__':
    sys.exit(main())
