# SPDX-License-Identifier: GPL-2.0-or-later
"""ELF symbols and libstdc++ containers for the shared state layout writer."""
import json
import os
from functools import lru_cache
from pathlib import Path
import re
import shutil
import subprocess


def install(layout):
    candidates = [os.environ.get('NEO98_DWARFDUMP'),
                  str(Path(os.environ['NEO98_LLVM_BIN']) / 'llvm-dwarfdump') if os.environ.get('NEO98_LLVM_BIN') else None,
                  str(layout.ROOT / 'build/toolchain/usr/lib/llvm-18/bin/llvm-dwarfdump'),
                  shutil.which('llvm-dwarfdump'), shutil.which('llvm-dwarfdump-18')]
    layout.DWARFDUMP = next((path for path in candidates if path and Path(path).is_file()), None)
    if not layout.DWARFDUMP: raise RuntimeError('llvm-dwarfdump is missing. Install llvm-18 or set NEO98_LLVM_BIN.')
    layout.CACHE = layout.CACHE / ('elf-' + __import__('hashlib').sha256(Path(__file__).read_bytes()).hexdigest()[:12])
    @lru_cache(maxsize=1024)
    def nm(path):
        text = subprocess.check_output(['nm', '-a', '-p', str(path)], text=True)
        source = ''; rows = []
        for line in text.splitlines():
            parts = line.split()
            if len(parts) != 3 or parts[1] in ('U', 'u'): continue
            address, kind, name = int(parts[0], 16), parts[1], parts[2]
            if kind == 'a' and name.endswith(('.cpp', '.c', '.S', '.o')):
                source = name
            elif kind in 'TtDdBbRrVvWwSs' and not name.startswith(('.', 'DW.ref.')):
                rows.append((address, kind, '_' + name, source))
        return rows
    def source_objects(binary):
        manifest = json.loads(Path(str(binary) + '.objects.json').read_text())
        return {Path(row['source']): Path(row['object']) for row in manifest
                if Path(row['source']).parent == layout.ROOT / 'src' and Path(row['source']).suffix == '.cpp'}
    def symbols(obj):
        return {address: name for address, kind, name, source in nm(obj)}
    @lru_cache(maxsize=1024)
    def sections(path):
        text = subprocess.check_output(['readelf','-WS',str(path)],text=True)
        found = {}
        for line in text.splitlines():
            match = re.match(r'\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)',line)
            if match: found[match[1]] = (match[2],int(match[3],16),int(match[4],16))
        return found
    @lru_cache(maxsize=1024)
    def elf_objects(path):
        found = []; names = sections(path)
        for line in subprocess.check_output(['readelf','-Ws',str(path)],text=True).splitlines():
            parts = line.split()
            if len(parts) < 8 or parts[3] != 'OBJECT' or parts[6] not in names: continue
            name, base, length = names[parts[6]]
            found.append((int(parts[1],16),int(parts[2],16 if parts[2].startswith('0x') else 10),'_'+parts[7],name))
        return found
    def data_symbols(obj):
        return {symbol for address,size,symbol,section in elf_objects(obj)
                if section.startswith(('.bss','.data')) and not section.startswith(('.data.rel.ro','.data.DW.ref.'))}
    def global_sizes(binary):
        return {address: size for address,size,symbol,section in elf_objects(binary) if size}

    # Cache each object's data names once, rather than run nm for each DIE.
    original_init = layout.Unit.__init__
    def init(unit, source, obj):
        unit.obj = obj; unit.elf_data = {name for address, kind, name, source in nm(obj)}
        original_init(unit, source, obj)
    @lru_cache(maxsize=1024)
    def address_symbols(path):
        rows = {}; debug = False
        for line in subprocess.check_output(['readelf','-Wr',str(path)],text=True).splitlines():
            if line.startswith('Relocation section '): debug = "'.rela.debug_addr'" in line
            if not debug: continue
            parts = line.split()
            if len(parts) < 7 or parts[2] != 'R_X86_64_64': continue
            value, symbol, extra = int(parts[3],16), parts[4], int(parts[6],16)
            if parts[5] == '-': extra = -extra
            target = '_' + symbol
            if symbol.startswith('.'):
                candidates = [name for at,size,name,section in elf_objects(path) if section == symbol and at == value+extra]
                target = candidates[0] if len(candidates) == 1 else None
            if target: rows[(int(parts[0],16)-8)//8] = target
        return rows
    @lru_cache(maxsize=16384)
    def variable_name(symbol):
        name = subprocess.check_output(['c++filt',symbol[1:]],text=True).strip()
        stop = name.rfind(')::')
        if stop >= 0:
            depth = 1; at = stop-1
            while at >= 0 and depth:
                depth += (name[at] == ')') - (name[at] == '('); at -= 1
            if not depth: name = name[:at+1]+'()'+name[stop+1:]
        return name
    def variable_cached(unit, die):
        location = die.attributes.get('DW_AT_location')
        if not location or not location.startswith(('DW_OP_addr ', 'DW_OP_addrx ')): return
        declaration = unit.dies.get(die.reference('DW_AT_specification') or die.reference('DW_AT_abstract_origin'), die)
        kind = declaration.reference('DW_AT_type') or die.reference('DW_AT_type')
        link = declaration.text('DW_AT_linkage_name') or die.text('DW_AT_linkage_name')
        symbol = '_' + link if link else None
        match = re.match(r'DW_OP_addrx (0x[0-9a-f]+)$',location)
        if not symbol and match: symbol = address_symbols(unit.obj).get(int(match[1],16))
        if symbol and kind is not None and symbol in unit.elf_data:
            name = variable_name(symbol) if symbol.startswith('__ZZ') else layout.qualified(declaration,unit.dies)
            unit.variables.append((name,symbol,kind))
    def linked_addresses(binary):
        objects = {Path(source).name: obj.name for source, obj in source_objects(binary).items()}
        by_object, by_name, sections, guards = {}, {}, {}, {}
        writable = {address for address,size,name,section in elf_objects(binary)
                    if section.startswith(('.data','.bss')) and not section.startswith('.data.rel.ro')}
        for address, kind, name, source in nm(binary):
            sections[address, name] = '__DATA,__data' if address in writable else '__TEXT,__const'
            if kind.islower() and source in objects: by_object[objects[source], name] = address
            by_name.setdefault(name, address)
            if name.startswith('__ZGV'): guards.setdefault(name, []).append(address)
        return by_object, by_name, sections, guards
    def program_symbols(binary):
        for address, kind, name, source in nm(binary):
            if address: yield address, name, source if kind.islower() else ''
    layout.source_objects, layout.symbols, layout.data_symbols = source_objects, symbols, data_symbols
    layout.sanitizer_global_sizes = global_sizes
    layout.Unit.__init__, layout.Unit.variable = init, variable_cached
    layout.linked_addresses, layout.program_symbols = linked_addresses, program_symbols
    original_check = layout.check_and_write
    def check(binary,output,types,variables,rules,quiet=False):
        # A source rule remains authoritative when ELF DWARF describes an
        # inline static that Mach-O needed to describe by its symbol.
        selected = {v['symbol']:v for v in variables if v['symbol'] in rules['symbol']}
        prior = layout.collect.unknown
        layout.collect.unknown = [*prior,*[(v['source'],v['symbol']) for v in selected.values()]]
        try: return original_check(binary,output,types,[v for v in variables if v['symbol'] not in selected],rules,quiet)
        finally: layout.collect.unknown = prior
    layout.check_and_write = check
    original_container = layout.container
    def inside(unit, die, depth=0):
        found = {}
        if die is None or depth > 10: return found
        for name, offset, kind in layout.members(unit, die):
            if name: found.setdefault(name, (offset, kind))
            child = unit.dies.get(kind)
            if child and child.tag in ('DW_TAG_structure_type', 'DW_TAG_class_type'):
                for inner, (at, target) in inside(unit, child, depth + 1).items(): found.setdefault(inner, (offset + at, target))
        return found
    def container(unit, die, table, depth):
        name = (die.text('DW_AT_name') or '').split('<')[0]
        size = die.number('DW_AT_byte_size', 0)
        parameters = [d.reference('DW_AT_type') for d in die.children if d.tag == 'DW_TAG_template_type_parameter']
        scope = die.parent
        while scope is not None and scope.tag == 'DW_TAG_namespace' and scope.text('DW_AT_name') != 'std': scope = scope.parent
        if scope is None or scope.text('DW_AT_name') != 'std': return original_container(unit, die, table, depth)
        if name not in ('vector', 'map', 'unique_ptr', 'shared_ptr', 'basic_string'): return None
        fields = inside(unit, die)
        if name == 'basic_string' and size == 32 and parameters:
            first = unit.dies.get(parameters[0])
            if first and first.number('DW_AT_byte_size', 0) == 1 and fields.get('_M_string_length', (None,))[0] == 8:
                return 'string', {'kind': 'string', 'size': 32, 'fields': []}
        if not parameters: return None
        if name == 'vector' and size == 24 and [fields.get(n, (None,))[0] for n in ('_M_start', '_M_finish', '_M_end_of_storage')] == [0, 8, 16]:
            element = layout.describe(unit, parameters[0], table, depth + 1)
            return f'vector<{element}>', {'kind':'vector', 'size':24, 'fields':[{'name':'element','offset':0,'type':element}]}
        if name in ('unique_ptr','shared_ptr') and size == (8 if name == 'unique_ptr' else 16):
            pointer = '_M_head_impl' if name == 'unique_ptr' else '_M_ptr'
            if fields.get(pointer, (None,))[0] != 0: return None
            if name == 'shared_ptr' and fields.get('_M_pi', (None,))[0] != 8: return None
            target = layout.describe(unit, parameters[0], table, depth + 1)
            kind = 'unique' if name == 'unique_ptr' else 'shared'
            return f'{kind}<{target}>', {'kind':kind,'size':size,'fields':[{'name':'target','offset':0,'type':target}]}
        if name != 'map' or size != 48 or len(parameters) < 2: return None
        if [fields.get(n, (None,))[0] for n in ('_M_color','_M_parent','_M_left','_M_right','_M_node_count')] != [8,16,24,32,40]: return None
        key = layout.describe(unit, parameters[0], table, depth + 1); mapped = layout.describe(unit, parameters[1], table, depth + 1)
        wanted = f'std::pair<{layout.spelled(unit.dies, parameters[0])},{layout.spelled(unit.dies, parameters[1])}>'
        node = None
        for candidate in unit.dies.values():
            if (candidate.text('DW_AT_name') or '').split('<')[0] != '_Rb_tree_node': continue
            first = next((d.reference('DW_AT_type') for d in candidate.children if d.tag == 'DW_TAG_template_type_parameter'), None)
            pair = layout.spelled(unit.dies, first)
            if re.sub(r'\bconst\b|\s', '', pair) == re.sub(r'\bconst\b|\s', '', wanted): node = candidate; break
        if node is None: return None
        links = inside(unit, node)
        if [links.get(n,(None,))[0] for n in ('_M_color','_M_parent','_M_left','_M_right')] != [0,8,16,24]: return None
        value = links.get('_M_storage',(None,))[0]
        if value is None: return None
        align = layout.alignment(table,mapped); second = (table[key]['size'] + align - 1)//align*align
        if value+second+table[mapped]['size'] > node.number('DW_AT_byte_size',0): return None
        table.setdefault('void', {'kind':'void','size':0})
        return f'map<{key},{mapped}>', {'kind':'map','size':48,'fields':[
            {'name':'key','offset':value,'type':key}, {'name':'mapped','offset':value+second,'type':mapped},
            {'name':'node','offset':node.number('DW_AT_byte_size',0),'type':'void'}]}
    layout.container = container
    layout.build_identity = lambda binary: bytes.fromhex(re.search(r'Build ID: ([0-9a-f]+)', subprocess.check_output(['readelf','-n',str(binary)],text=True,env=dict(os.environ,LC_ALL='C'))).group(1))[:16]
