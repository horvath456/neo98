#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The SDK for ARM64 applications (docs/ARM64-APPS.md).

sdk/api/*.api is the one source of the signatures. This tool reads it and
writes

    build/sdk/include/windows.h the header a program includes
    build/sdk/lib/<dll>.lib import libraries, by name
    build/sdk/lib/start.lib start code and compiler helpers
    build/generated/arm64_api_table.inc the table src/arm64_cpu.cpp converts by

and builds a program against them:

    python3 neo98.py sdk-build source.c [more.c] [resources.rc] -o program.exe [--gui]

The compiler is the clang of Xcode, with --target=aarch64-pc-windows-msvc for
C and --target=aarch64-w64-windows-gnu for programs with C++, the linker
lld-link of Homebrew. No file of Microsoft is involved.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
API = ROOT / 'sdk/api'
SDK = ROOT / 'build/sdk'
TABLE = ROOT / 'build/generated/arm64_api_table.inc'

# Class of a scalar: (size in Win32, size in Win64, how a 32-bit value widens).
# 'copy' classes have the same bytes on both sides.
CLASSES = {
    'void': (0, 0), 's8': (1, 1), 'u8': (1, 1), 's16': (2, 2), 'u16': (2, 2),
    's32': (4, 4), 'u32': (4, 4), 's64': (8, 8), 'u64': (8, 8), 'f32': (4, 4), 'f64': (8, 8),
    'ptr': (4, 8), 'handle': (4, 8), 'sptr': (4, 8), 'uptr': (4, 8), 'fn': (4, 8),
    # A count of bytes (SIZE_T). Like uptr, but one that 32 bits cannot hold
    # reaches the system as the largest they can: asking for 4 GiB and a page
    # fails for want of memory instead of succeeding with a page.
    'usize': (4, 8),
}
# Kinds of the generated table; src/arm64_api.h names the same numbers.
KIND = {'void': 0, 's32': 1, 'u32': 1, 's8': 1, 'u8': 1, 's16': 1, 'u16': 1, 'ptr': 2, 'handle': 3, 'sptr': 4,
        'uptr': 5, 's64': 6, 'u64': 6, 'f32': 7, 'f64': 8, 'fn': 9, 'struct': 10, 'varargs': 11, 'usize': 12}
# keep: the system holds on to the struct after the call (a WAVEHDR between
# waveOutPrepareHeader and waveOutUnprepareHeader); the processor gives it a
# lasting 32-bit form. drop: the call that ends that, when it returns zero.
DIRECTION = {'in': 1, 'out': 2, 'inout': 3, 'keep': 7, 'drop': 11}
FIELD = {'copy': 0, 'zext': 1, 'sext': 2, 'fn': 3, 'size': 4, 'deep': 5}


class ApiError(Exception):
    pass


class Model:
    def __init__(self):
        self.types = {}       # name -> dict(cls, c, callback)
        self.type_order = []
        self.structs = {}     # name -> dict(fields, opaque, order)
        self.callbacks = {}   # name -> dict(ret, params, message)
        self.constants = []   # (name, value)
        self.messages = []    # dict(name, value, which, direction, struct)
        self.functions = []   # dict(dll, name, ret, params, provider, cdecl, attr, varargs)
        self.order = []       # ('type'|'struct'|'callback', name) in source order, for the header
        self.objects = []     # (GDI object type, struct): what GetObject fills
        self.hook_kinds = []  # (hook type, callback): the procedure SetWindowsHookEx takes for it
        self.codes = []       # dict(callback, value, direction, struct): lParam of a hook by its code


def strip_comments(text):
    return re.sub(r'#[^\n]*', '', text)


def statements(text, path):
    """Split into statements at ';' outside braces, with the line of each."""
    out, depth, start, line, first = [], 0, 0, 1, 1
    for i, c in enumerate(text):
        if c == '\n':
            line += 1
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif c == ';' and depth == 0:
            body = text[start:i].strip()
            if body:
                out.append((body, first))
            start = i + 1
            first = line
        if text[start:i + 1].strip() == '':
            first = line
    if text[start:].strip():
        raise ApiError(f'{path}: statement without ";" at the end: {text[start:].strip()[:40]!r}')
    return out


def parse_params(model, text, where):
    params, varargs = [], False
    text = text.strip()
    if text in ('', 'void'):
        return params, varargs
    for index, part in enumerate(p.strip() for p in re.split(r',(?![^(]*\))', text)):
        if part == '...':
            varargs = True
            continue
        m = re.fullmatch(r'(?:(in|out|inout|keep|drop)\s+)?(\w+)\s*(\*)?\s*(\w+)?((?:\s+\w+\([^)]*\))*)(\s+passback)?', part)
        if not m:
            raise ApiError(f'{where}: cannot read parameter {part!r}')
        direction, kind, star, name, relations, passback = m.groups()
        # count(p): p holds the number of elements this pointer leads to.
        # sizeof(p): this parameter holds the size of the struct p points at.
        # callback(K): this integer is a procedure of kind K.
        # when(p, mask, value):... but only when p & mask == value.
        # shadow(S): this integer is the address of a struct S the system kept.
        # receives(T): this pointer leads to where the system writes a value of
        # type T (FormatMessage with FORMAT_MESSAGE_ALLOCATE_BUFFER hands its
        # buffer out through the pointer that otherwise is the buffer).
        relation = {}
        for word, argument in re.findall(r'(\w+)\(([^)]*)\)', relations):
            if word not in ('count', 'sizeof', 'callback', 'when', 'shadow', 'select', 'receives'):
                raise ApiError(f'{where}: unknown relation {word}()')
            relation[word] = argument.strip()
        when = None
        if 'when' in relation:
            parts = [x.strip() for x in relation['when'].split(',')]
            if len(parts) != 3:
                raise ApiError(f'{where}: when(parameter, mask, value)')
            when = (parts[0], int(parts[1], 0), int(parts[2], 0))
        params.append({'direction': direction, 'type': kind, 'pointer': bool(star),
                       'name': name or f'p{index}', 'count': relation.get('count'),
                       'sizeof': relation.get('sizeof'), 'callback': relation.get('callback'),
                       'shadow': relation.get('shadow'), 'when': when, 'select': relation.get('select'),
                       'receives': relation.get('receives'),
                       'passback': bool(passback)})
    return params, varargs


def split_fields(text):
    """The members of a struct body: split at ';' outside braces."""
    out, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif c == ';' and depth == 0:
            out.append(text[start:i].strip())
            start = i + 1
    if text[start:].strip():
        out.append(text[start:].strip())
    return [item for item in out if item]


def parse_fields(text, where):
    """A field is `TYPE [*] name [[n]] [sizeof]`, or a nested `union {... }
    [name]` or `struct {... } [name]`."""
    fields = []
    for field in split_fields(text):
        if n := re.fullmatch(r'(union|struct)\s*\{(.*)\}\s*(\w+)?', field, re.S):
            fields.append({'group': n[1], 'members': parse_fields(n[2], where), 'name': n[3]})
            continue
        f = re.fullmatch(r'(\w+)\s*(\*)?\s*(\w+)(?:\[(\w+)\])?(\s+sizeof|\s+deep)?', field)
        if not f:
            raise ApiError(f'{where}: cannot read field {field!r}')
        fields.append({'type': f[1], 'pointer': bool(f[2]), 'name': f[3],
                       'count': int(f[4], 0) if f[4] else 0, 'sizeof': (f[5] or '').strip() == 'sizeof',
                       'deep': (f[5] or '').strip() == 'deep'})
    return fields


def parse(paths):
    model = Model()
    dll = None
    for path in paths:
        for body, line in statements(strip_comments(path.read_text()), path):
            where = f'{path.name}:{line}'
            if m := re.fullmatch(r'type\s+(\w+)\s+(\w+)\s+"([^"]*)"', body):
                name, cls, c = m.groups()
                if cls not in CLASSES:
                    raise ApiError(f'{where}: unknown class {cls}')
                model.types[name] = {'cls': cls, 'c': c}
                model.order.append(('type', name))
            elif m := re.fullmatch(r'handle\s+(\w+)', body):
                model.types[m[1]] = {'cls': 'handle', 'c': None}
                model.order.append(('type', m[1]))
            elif m := re.fullmatch(r'struct\s+(\w+)\s*((?:(?:opaque|packed)\s*)*)\{(.*)\}', body, re.S):
                # packed: the layout of `#pragma pack(1)`, which mmsystem.h uses.
                words = m[2].split()
                model.structs[m[1]] = {'fields': parse_fields(m[3], where), 'opaque': 'opaque' in words,
                                       'packed': 'packed' in words}
                model.order.append(('struct', m[1]))
            elif m := re.fullmatch(r'callback\s+(\w+)\s+(\w+)\s*\((.*)\)\s*(message)?', body, re.S):
                params, _ = parse_params(model, m[3], where)
                model.callbacks[m[2]] = {'ret': m[1], 'params': params, 'message': bool(m[4])}
                model.types[m[2]] = {'cls': 'fn', 'c': None, 'callback': m[2]}
                model.order.append(('callback', m[2]))
            elif m := re.fullmatch(r'const\s+(\w+)\s+(.+)', body, re.S):
                if any(name == m[1] for name, _ in model.constants):
                    raise ApiError(f'{where}: constant {m[1]} is defined twice')
                model.constants.append((m[1], m[2].strip()))
            elif m := re.fullmatch(r'message\s+(\w+)\s+(\w+)(?:\s+(wparam|lparam)\s+(in|out|inout|keep)\s+(\w+))?', body):
                model.messages.append({'name': m[1], 'value': int(m[2], 0), 'which': m[3], 'direction': m[4],
                                       'struct': m[5], 'where': where})
            elif m := re.fullmatch(r'hook\s+(-?\w+)\s+(\w+)', body):
                model.hook_kinds.append((int(m[1], 0), m[2], where))
            elif m := re.fullmatch(r'code\s+(\w+)\s+(\w+)\s+lparam\s+(in|out|inout)\s+(\w+)', body):
                model.codes.append({'callback': m[1], 'value': int(m[2], 0), 'direction': m[3], 'struct': m[4], 'where': where})
            elif m := re.fullmatch(r'object\s+(\w+)\s+(\w+)', body):
                model.objects.append((int(m[1], 0), m[2], where))
            elif m := re.fullmatch(r'dll\s+(\w+)', body):
                dll = m[1].upper() + '.DLL'
            elif m := re.fullmatch(r'(\w+)\s*(\*)?\s*(\w+)\s*\((.*?)\)\s*(?:=\s*(\w+))?\s*((?:\w+\s*)*)', body, re.S):
                if not dll:
                    raise ApiError(f'{where}: function before any "dll" line')
                if m[2]:
                    raise ApiError(f'{where}: a return type is a named type, not a pointer')
                params, varargs = parse_params(model, m[4], where)
                attrs = m[6].split()
                unknown = set(attrs) - {'cdecl', 'message', 'windowlong', 'classlong', 'sizeresult', 'object', 'core'}
                if unknown:
                    raise ApiError(f'{where}: unknown attribute {sorted(unknown)}')
                model.functions.append({'dll': dll, 'name': m[3], 'ret': m[1], 'params': params,
                                        'provider': m[5] or m[3], 'attrs': attrs, 'varargs': varargs,
                                        'where': where})
            else:
                raise ApiError(f'{where}: cannot read {body[:60]!r}')
    return model


def layout_fields(model, fields, wide, packed, seen, owner):
    size, alignment, flat = 0, 1, []
    for field in fields:
        count = field.get('count') or 1
        if 'group' in field:
            if field['group'] == 'struct':
                item = layout_fields(model, field['members'], wide, packed, seen, owner)
            else:
                # A union has the room of its largest member; a conversion
                # follows the first one.
                parts = [layout_fields(model, [member], wide, packed, seen, owner) for member in field['members']]
                item = (max(part[0] for part in parts), max(part[1] for part in parts), parts[0][2])
        elif field.get('sizeof'):
            # The field holds the size of the struct itself (cbSize).
            item = (4, 4, [(0, 'size', 4, None)])
        elif field['pointer']:
            # deep: the struct the pointer leads to crosses with this one.
            item = (8 if wide else 4, 8 if wide else 4,
                    [(0, 'deep' if field.get('deep') else 'ptr', 8 if wide else 4, field['type'] if field.get('deep') else None)])
        elif field['type'] in model.structs:
            item = layout(model, field['type'], wide, seen + (owner,))
        elif field['type'] in model.types:
            t = model.types[field['type']]
            n = CLASSES[t['cls']][1 if wide else 0]
            if not n:
                raise ApiError(f'struct {owner}: field {field["name"]} has no size')
            item = (n, n, [(0, t['cls'], n, t.get('callback'))])
        else:
            raise ApiError(f'struct {owner}: unknown type {field["type"]}')
        isize, ialign, iflat = item
        if packed:
            ialign = 1
        size = (size + ialign - 1) // ialign * ialign
        for k in range(count):
            for offset, cls, n, callback in iflat:
                flat.append((size + k * isize + offset, cls, n, callback))
        size += isize * count
        alignment = max(alignment, ialign)
    size = (size + alignment - 1) // alignment * alignment
    return size, alignment, flat


def layout(model, name, wide, seen=()):
    """(size, alignment, [(offset, field class, bytes, callback)]) of a struct."""
    if name in seen:
        raise ApiError(f'struct {name} contains itself')
    struct = model.structs[name]
    return layout_fields(model, struct['fields'], wide, struct.get('packed'), seen, name)


def struct_program(model, name):
    """The conversion of one struct: None when both layouts are the same
    bytes, else (size32, size64, [(field kind, offset32, offset64, bytes, callback)])."""
    size32, _, flat32 = layout(model, name, False)
    size64, _, flat64 = layout(model, name, True)
    same = size32 == size64 and all(a[0] == b[0] and a[2] == b[2] for a, b in zip(flat32, flat64))
    if same and not any(cls == 'fn' for _, cls, _, _ in flat32):
        return None
    ops = []
    for (off32, cls, n32, callback), (off64, _, n64, _) in zip(flat32, flat64):
        if cls == 'size':
            ops.append(('size', off32, off64, 0, None))
        elif n32 == n64:
            if ops and ops[-1][0] == 'copy' and ops[-1][1] + ops[-1][3] == off32 and ops[-1][2] + ops[-1][3] == off64:
                ops[-1] = ('copy', ops[-1][1], ops[-1][2], ops[-1][3] + n32, None)
            else:
                ops.append(('copy', off32, off64, n32, None))
        elif cls == 'fn':
            ops.append(('fn', off32, off64, 0, callback))
        elif cls == 'deep':
            ops.append(('deep', off32, off64, 0, callback))
        else:
            ops.append(('sext' if cls in ('handle', 'sptr') else 'zext', off32, off64, 0, None))
    return size32, size64, ops


def scalar_struct(model, type_name):
    """A pointer to one pointer-sized scalar converts like a struct of one field."""
    name = f'__{type_name}'
    if name not in model.structs:
        model.structs[name] = {'fields': [{'type': type_name, 'pointer': False, 'name': 'value', 'count': 0}],
                               'opaque': False, 'synthetic': True}
    return name


def classify_param(model, p, where):
    """(kind, auxiliary, direction, count parameter name, 32-bit slots)."""
    t = p['type']
    if p.get('receives'):
        if p['receives'] not in model.types or model.types[t]['cls'] != 'ptr':
            raise ApiError(f'{where}: receives(TYPE) belongs to a pointer and names a scalar type')
        return ('struct', scalar_struct(model, p['receives']), 'out', None, 1)
    if p.get('callback') or p.get('shadow'):
        cls = model.types.get(t, {}).get('cls')
        if p['pointer'] or cls not in ('ptr', 'sptr', 'uptr', 'handle'):
            raise ApiError(f'{where}: callback() and shadow() belong to a pointer-sized value')
        if p.get('callback'):
            if p['callback'] not in model.callbacks:
                raise ApiError(f'{where}: unknown callback {p["callback"]}')
            return ('fn', p['callback'], None, None, 1)
        if p['shadow'] not in model.structs or struct_program(model, p['shadow']) is None:
            raise ApiError(f'{where}: shadow({p["shadow"]}) names no converted struct')
        return ('struct', p['shadow'], 'keep', None, 1)
    if p['pointer']:
        if t in model.structs:
            target = t
        elif t in model.types and model.types[t]['cls'] != 'void':
            cls = model.types[t]['cls']
            if CLASSES[cls][0] == CLASSES[cls][1] and cls != 'fn':
                return ('ptr', None, None, None, 1)
            target = scalar_struct(model, t)
        elif t in model.types:
            return ('ptr', None, None, None, 1)
        else:
            raise ApiError(f'{where}: unknown type {t}')
        if model.structs[target]['opaque'] or struct_program(model, target) is None:
            return ('ptr', None, None, None, 1)
        if not p['direction']:
            raise ApiError(f'{where}: {t}* differs between Win32 and Win64 and needs in, out or inout')
        return ('struct', target, p['direction'], p['count'], 1)
    if t in model.structs:
        size32, _, _ = layout(model, t, False)
        size64, _, _ = layout(model, t, True)
        if size32 != 8 or size64 != 8:
            raise ApiError(f'{where}: struct {t} by value is supported for 8-byte structs only')
        return ('u64', None, None, None, 2)
    if t not in model.types:
        raise ApiError(f'{where}: unknown type {t}')
    cls = model.types[t]['cls']
    if cls == 'void':
        raise ApiError(f'{where}: void parameter')
    if cls == 'fn':
        return ('fn', model.types[t]['callback'], None, None, 1)
    return (cls, None, None, None, 2 if cls in ('s64', 'u64', 'f64') else 1)


def read_specs():
    text = (ROOT / 'src/x86_app.cpp').read_text()
    start = text.index('const ApiSpec specs[]')
    end = text.index('};', start)
    specs = {}
    for dll, name, argc in re.findall(r'\{"([^"]+)",\s*"([^"]+)",\s*(0x[0-9a-fA-F]+u?|\d+)\}', text[start:end]):
        specs[(dll.upper(), name)] = int(argc.rstrip('u'), 0)
    return specs


def check(model):
    """Every function names a connected export, and its 32-bit slots are the
    argument count the provider reads."""
    specs = read_specs()
    problems = []
    for f in model.functions:
        # core: the processor of an ARM64 process answers the function itself
        # (src/arm64_seh.inc); no provider reads a 32-bit frame for it.
        if 'core' in f['attrs']:
            continue
        key = (f['dll'], f['provider'])
        if key not in specs:
            problems.append(f'{f["where"]}: {f["dll"]}!{f["provider"]} is not connected in specs[]')
            continue
        slots = sum(classify_param(model, p, f['where'])[4] for p in f['params'])
        argc = specs[key]
        if argc & 0x60000000:
            problems.append(f'{f["where"]}: {f["provider"]} has no native handler (argc {argc:#x})')
        if (argc & 255) != slots:
            problems.append(f'{f["where"]}: {f["name"]} has {slots} slots, specs[] says {argc & 255}')
        if bool(argc & 0x80000000) != ('cdecl' in f['attrs']):
            problems.append(f'{f["where"]}: {f["name"]} cdecl differs from specs[]')
    if problems:
        raise ApiError('\n'.join(problems))


def c_type(model, name):
    return name


def header(model):
    out = ['/* Generated by tools/arm64_sdk.py from sdk/api. Do not edit. */', '#ifndef NEO98_WINDOWS_H',
           '#define NEO98_WINDOWS_H', '#ifndef RC_INVOKED', '#ifdef __cplusplus', 'extern "C" {', '#endif', '']
    out.append((API / 'prelude.h').read_text().rstrip())
    out.append('')
    for kind, name in model.order:
        if kind == 'type':
            t = model.types[name]
            if t['c'] is None:
                out.append(f'typedef struct {name}__ {{ int unused; }} *{name};')
            elif t['c'] != name:
                out.append(f'typedef {t["c"]} {name};' if '%' not in t['c'] else 'typedef ' + t['c'].replace('%', name) + ';')
        elif kind == 'struct':
            s = model.structs[name]

            def members(fields, indent):
                for f in fields:
                    if 'group' in f:
                        out.append(f'{indent}{f["group"]} {{')
                        members(f['members'], indent + '    ')
                        out.append(f'{indent}}}{" " + f["name"] if f["name"] else ""};')
                        continue
                    star = '*' if f['pointer'] else ''
                    array = f'[{f["count"]}]' if f['count'] else ''
                    kind = f'struct tag{name}' if f['type'] == name else f['type']
                    out.append(f'{indent}{kind} {star}{f["name"]}{array};')
            if s.get('packed'):
                out.append('#pragma pack(push, 1)')
            out.append(f'typedef struct tag{name} {{')
            members(s['fields'], '    ')
            out.append(f'}} {name}, *P{name}, *LP{name};')
            if s.get('packed'):
                out.append('#pragma pack(pop)')
            if name.endswith('A') and name[:-1] not in model.structs and name[:-1] not in model.types:
                out.append(f'typedef {name} {name[:-1]}, *P{name[:-1]}, *LP{name[:-1]};')
        else:
            cb = model.callbacks[name]
            params = ', '.join(('' if not p['pointer'] else '') + p['type'] + (' *' if p['pointer'] else '')
                               for p in cb['params']) or 'void'
            out.append(f'typedef {cb["ret"]} (__stdcall *{name})({params});')
    # What a C compiler must see as written: the records of exceptions and
    # unwinding, which only a 64-bit program has in this form.
    out += ['', (API / 'exceptions.h').read_text().rstrip(), '']
    out += ['#ifdef __cplusplus', '}', '#endif', '#endif /* RC_INVOKED */', '']
    seen = set()
    for name, value in model.constants:
        out.append(f'#define {name} {value}')
        seen.add(name)
    for m in model.messages:
        if m['name'] not in seen:
            out.append(f'#define {m["name"]} 0x{m["value"]:04X}')
            seen.add(m['name'])
    # The resource compiler takes the kind of a resource as a number.
    out += ['', '#ifdef RC_INVOKED', '#define RT_MANIFEST 24', '#endif']
    out += ['', '#ifndef RC_INVOKED', '#ifdef __cplusplus', 'extern "C" {', '#endif']
    names = {f['name'] for f in model.functions}
    for f in model.functions:
        params = []
        for p in f['params']:
            const = 'const ' if p['pointer'] and p['direction'] == 'in' else ''
            params.append(f'{const}{p["type"]} {"*" if p["pointer"] else ""}{p["name"]}')
        if f['varargs']:
            params.append('...')
        convention = '__cdecl' if 'cdecl' in f['attrs'] else '__stdcall'
        out.append(f'__declspec(dllimport) {f["ret"]} {convention} {f["name"]}({", ".join(params) or "void"});')
    out.append('')
    for f in model.functions:
        if f['name'].endswith('A') and f['name'][:-1] not in names:
            out.append(f'#define {f["name"][:-1]} {f["name"]}')
    out += ['', (API / 'epilogue.h').read_text().rstrip(), '', '#ifdef __cplusplus', '}', '#endif',
            '#endif /* RC_INVOKED */', '#endif', '']
    return '\n'.join(out)


def table(model):
    """The table of src/arm64_cpu.cpp; src/arm64_api.h declares the records."""
    struct_ids, struct_rows, field_rows = {}, [], []
    callback_ids = {name: i for i, name in enumerate(model.callbacks)}

    def struct_id(name):
        if name not in struct_ids:
            program = struct_program(model, name)
            size32, size64, ops = program
            struct_ids[name] = len(struct_rows)
            struct_rows.append([name, size32, size64, len(field_rows), len(ops)])
            first = len(field_rows)
            field_rows.extend([None] * len(ops))
            for i, (kind, off32, off64, n, callback) in enumerate(ops):
                field_rows[first + i] = (FIELD[kind], off32, off64, n if kind == 'copy' else
                                         callback_ids[callback] if kind == 'fn' else
                                         struct_id(callback) if kind == 'deep' else 0)
        return struct_ids[name]

    def param_row(model, p, where, names):
        kind, aux, direction, count, _ = classify_param(model, p, where)
        when = (0xff, 0, 0)
        if p.get('when'):
            if p['when'][0] not in names:
                raise ApiError(f'{where}: when({p["when"][0]}, ...) names no parameter')
            when = (names.index(p['when'][0]), p['when'][1], p['when'][2])
        if kind == 'struct':
            index = 0xff
            if count:
                if count not in names:
                    raise ApiError(f'{where}: count({count}) names no parameter')
                index = names.index(count)
            return (KIND['struct'], struct_id(aux), DIRECTION[direction], index, *when)
        if kind == 'fn':
            # select(p): the kind follows from the value of p (the hook type).
            if p.get('select'):
                if p['select'] not in names:
                    raise ApiError(f'{where}: select({p["select"]}) names no parameter')
                return (KIND['fn'], callback_ids[aux], 1, names.index(p['select']), *when)
            return (KIND['fn'], callback_ids[aux], 0, 0xff, *when)
        if p.get('sizeof'):
            if p['sizeof'] not in names:
                raise ApiError(f'{where}: sizeof({p["sizeof"]}) names no parameter')
            return (KIND[kind], 0, 0, names.index(p['sizeof']), *when)
        # passback: a pointer the system handed to a procedure of the program
        # and the program hands on (lParam of CallNextHookEx).
        return (KIND[kind], 0, 4 if p.get('passback') else 0, 0xff, *when)

    def return_kind(model, name, where):
        if name not in model.types:
            raise ApiError(f'{where}: unknown return type {name}')
        cls = model.types[name]['cls']
        if cls in ('f32', 'f64'):
            raise ApiError(f'{where}: a floating-point result is not supported')
        return KIND[cls]

    param_rows, function_rows, callback_rows = [], [], []
    for name, cb in model.callbacks.items():
        first = len(param_rows)
        names = [p['name'] for p in cb['params']]
        for p in cb['params']:
            row = param_row(model, p, f'callback {name}', names)
            if row[0] == KIND['fn']:
                raise ApiError(f'callback {name}: a callback parameter of a callback is not supported')
            param_rows.append(row)
        coded = any(c['callback'] == name for c in model.codes)
        callback_rows.append((name, return_kind(model, cb['ret'], f'callback {name}'), first, len(cb['params']),
                              1 if cb['message'] else 2 if coded else 0))
    for f in model.functions:
        first = len(param_rows)
        names = [p['name'] for p in f['params']]
        for p in f['params']:
            param_rows.append(param_row(model, p, f['where'], names))
        if f['varargs']:
            param_rows.append((KIND['varargs'], 0, 0, 0xff, 0xff, 0, 0))
        flags = 0
        if 'message' in f['attrs']:
            classes = [model.types.get(p['type'], {}).get('cls') if not p['pointer'] else None for p in f['params']]
            position = next((i for i in range(1, len(classes) - 1)
                             if f['params'][i]['type'] == 'WPARAM' and f['params'][i + 1]['type'] == 'LPARAM'), None)
            if position is None:
                raise ApiError(f'{f["where"]}: "message" needs UINT, WPARAM, LPARAM parameters')
            flags |= 1 | (position - 1) << 8
        if 'windowlong' in f['attrs']:
            flags |= 2
        if 'classlong' in f['attrs']:
            flags |= 4
        if 'sizeresult' in f['attrs']:
            flags |= 8
        if 'core' in f['attrs']:
            flags |= 32
        if 'object' in f['attrs']:
            # (handle, bytes, buffer): the struct follows from the type of the object.
            if [classify_param(model, p, f['where'])[0] for p in f['params']] != ['handle', 's32', 'ptr']:
                raise ApiError(f'{f["where"]}: "object" needs (handle, int, pointer) parameters')
            flags |= 16
        function_rows.append((f['dll'], f['name'], f['provider'], return_kind(model, f['ret'], f['where']), first,
                              len(param_rows) - first, flags))
    message_rows = []
    for m in sorted(model.messages, key=lambda m: m['value']):
        if not m['which']:
            continue
        if m['struct'] not in model.structs:
            raise ApiError(f'{m["where"]}: unknown struct {m["struct"]}')
        if struct_program(model, m['struct']) is None:
            continue
        message_rows.append((m['value'], 1 if m['which'] == 'lparam' else 0, DIRECTION[m['direction']],
                             struct_id(m['struct']), m['name']))
    code_rows = []
    for c in model.codes:
        if c['callback'] not in callback_ids or c['struct'] not in model.structs:
            raise ApiError(f'{c["where"]}: unknown callback or struct')
        if struct_program(model, c['struct']) is not None:
            code_rows.append((callback_ids[c['callback']], c['value'], DIRECTION[c['direction']], struct_id(c['struct'])))
    hook_rows = []
    for value, name, where in model.hook_kinds:
        if name not in callback_ids:
            raise ApiError(f'{where}: unknown callback {name}')
        hook_rows.append((value, callback_ids[name]))
    object_rows = []
    for kind, name, where in model.objects:
        if name not in model.structs:
            raise ApiError(f'{where}: unknown struct {name}')
        if struct_program(model, name) is not None:
            object_rows.append((kind, struct_id(name), name))
    digest = hashlib.sha256(repr((function_rows, param_rows, struct_rows, field_rows, callback_rows,
                                  message_rows, object_rows, code_rows, hook_rows)).encode()).hexdigest()[:16]
    out = ['// Generated by tools/arm64_sdk.py from sdk/api. Do not edit.',
           f'// {len(function_rows)} functions, {len(struct_rows)} converted structs, {len(callback_rows)} callbacks, '
           f'{len(message_rows)} messages. Identity {digest}.',
           'static const Arm64Api::Field arm64_api_fields[] = {']
    out += [f'    {{{k}, {a}, {b}, {n}}},' for k, a, b, n in field_rows] or ['    {0, 0, 0, 0},']
    out += ['};', 'static const Arm64Api::Struct arm64_api_structs[] = {']
    out += [f'    {{"{n}", {a}, {b}, {first}, {count}}},' for n, a, b, first, count in struct_rows] or ['    {"", 0, 0, 0, 0},']
    out += ['};', 'static const Arm64Api::Param arm64_api_params[] = {']
    out += [f'    {{{k}, {a}, {d}, {c}, {w}, 0x{m:x}, 0x{v:x}}},' for k, a, d, c, w, m, v in param_rows] or ['    {0, 0, 0, 0, 0xff, 0, 0},']
    out += ['};', 'static const Arm64Api::Callback arm64_api_callbacks[] = {']
    out += [f'    {{"{n}", {r}, {first}, {count}, {message}}},' for n, r, first, count, message in callback_rows] or ['    {"", 0, 0, 0, 0},']
    out += ['};', 'static const Arm64Api::Message arm64_api_messages[] = {']
    out += [f'    {{0x{v:04x}, {w}, {d}, {s}}}, // {n}' for v, w, d, s, n in message_rows] or ['    {0, 0, 0, 0},']
    out += ['};', 'static const Arm64Api::Code arm64_api_codes[] = {']
    out += [f'    {{{c}, {v}, {d}, {t}}},' for c, v, d, t in code_rows] or ['    {0, 0, 0, 0},']
    out += ['};', 'static const Arm64Api::HookKind arm64_api_hook_kinds[] = {']
    out += [f'    {{{v}, {c}}},' for v, c in hook_rows] or ['    {0, 0},']
    out += ['};', 'static const Arm64Api::Object arm64_api_objects[] = {']
    out += [f'    {{{k}, {s}}}, // {n}' for k, s, n in object_rows] or ['    {0, 0},']
    out += ['};', 'static const Arm64Api::Function arm64_api_functions[] = {']
    out += [f'    {{"{dll}", "{name}", "{provider}", {ret}, {first}, {count}, 0x{flags:x}}},'
            for dll, name, provider, ret, first, count, flags in function_rows]
    out += ['};',
            f'static const unsigned arm64_api_struct_count = {len(struct_rows)}, arm64_api_callback_count = {len(callback_rows)}, '
            f'arm64_api_message_count = {len(message_rows)}, arm64_api_function_count = {len(function_rows)}, '
            f'arm64_api_object_count = {len(object_rows)}, arm64_api_code_count = {len(code_rows)}, '
            f'arm64_api_hook_kind_count = {len(hook_rows)};',
            '// The struct a message travels in (GetMessage, DispatchMessage), or -1.',
            f'static const int arm64_api_msg_struct = {struct_ids.get("MSG", -1)};', '']
    return '\n'.join(out)


def load():
    paths = sorted(API.glob('*.api'))
    if not paths:
        raise ApiError(f'no signature files under {API}')
    model = parse(paths)
    check(model)
    return model


def write_if_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)
        return True
    return False


def generate_table():
    """build/generated/arm64_api_table.inc; cheap, called by every build."""
    model = load()
    write_if_changed(TABLE, table(model))
    return model


def tool(name):
    for candidate in (shutil.which(name), f'/opt/homebrew/bin/{name}', f'/opt/homebrew/opt/llvm/bin/{name}',
                      f'/opt/homebrew/opt/lld/bin/{name}'):
        if candidate and Path(candidate).exists():
            return candidate
    raise ApiError(f'{name} not found; the SDK needs clang of Xcode and lld-link of Homebrew (brew install lld)')


# -nostdlibinc: the headers of the compiler itself (stdint.h, stdarg.h,
# stddef.h, arm_neon.h) stay, the ones of the host system do not.
# -funwind-tables: every function gets its entry in.pdata. Without it clang
# writes one only for a function with a handler, and an exception could not
# pass a frame of any other (measured: a freestanding C file has none).
# Allocation failure must call our Windows allocator and set errno even when
# Clang can prove that a constant allocation cannot succeed.
CFLAGS = ['--target=aarch64-pc-windows-msvc', '-fno-stack-protector',
          '-fno-builtin-malloc', '-fno-builtin-calloc', '-fno-builtin-realloc',
          '-nostdlibinc', '-funwind-tables', '-O2', '-Wall', '-Wno-pragma-pack', '-Wno-literal-range']
RUNTIME_CFLAGS = [*CFLAGS, '-ffreestanding', '-fno-builtin']
CRT = ROOT / 'sdk/crt'
MUSL = ROOT / 'third_party/musl-math'
MUSL_SHIM = CRT / 'musl-shim'


# C++ is built for the MinGW target of the same processor and the same
# calling convention: the Itanium C++ ABI, for which libc++, libc++abi and
# libunwind exist in source and need nothing of Microsoft's runtime. The
# target of Microsoft's ABI (the one C is built for) would need its C++
# runtime, which no free source provides. A program with C++ sources is built
# for the MinGW target as a whole, its C sources too, so that one program
# sees one set of predefined macros. docs/ARM64-APPS.md, "C++".
GNU_TARGET = '--target=aarch64-w64-windows-gnu'
CXX = ROOT / 'sdk/cxx'
LIBCXX_STANDARD = '-std=c++23'   # what LLVM builds its runtimes with
CXXFLAGS = [GNU_TARGET, '-nostdlibinc', '-nostdinc++', '-funwind-tables', '-fno-stack-protector', '-O2', '-Wall',
            '-Wno-pragma-pack', '-Wno-literal-range', '-std=c++17']


def gnu(flags):
    """The flags of a C compilation, for the MinGW target."""
    return [GNU_TARGET if flag.startswith('--target=') else flag for flag in flags]


def cxx_includes(sdk):
    return ['-isystem', str(sdk / 'include/c++/v1'), '-isystem', str(sdk / 'include')]


def source_stamp():
    digest = hashlib.sha256()
    import arm64_dx
    for path in [*arm64_dx.sources(), *sorted(API.glob('*')), *sorted((ROOT / 'sdk/start').glob('*')), Path(__file__),
                 *sorted(p for p in CRT.rglob('*') if p.is_file()), *sorted(p for p in (ROOT / 'sdk/lib').rglob('*') if p.is_file()),
                 ROOT / 'sdk/dependencies.json', ROOT / 'src/x86_app.cpp']:
        digest.update(path.name.encode())
        digest.update(str(path.stat().st_mtime_ns).encode() if path.name == 'x86_app.cpp' else path.read_bytes())
    return digest.hexdigest()


def generate(force=False):
    """The whole SDK under build/sdk. Rebuilt when its sources changed."""
    # Several tests of the suite may ask for the SDK at once: one generates,
    # the others wait and find it current.
    import fcntl
    SDK.mkdir(parents=True, exist_ok=True)
    with open(SDK / '.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return _generate_locked(force)


def _generate_locked(force):
    stamp = SDK / 'stamp'
    identity = source_stamp()
    if not force and stamp.exists() and stamp.read_text() == identity and TABLE.exists():
        return SDK
    model = generate_table()
    include, lib = SDK / 'include', SDK / 'lib'
    include.mkdir(parents=True, exist_ok=True)
    lib.mkdir(parents=True, exist_ok=True)
    write_if_changed(include / 'windows.h', header(model))
    write_if_changed(include / 'windowsx.h', (API / 'windowsx.h').read_text())
    # The headers of Windows a program may name on its own; all is in windows.h.
    for name in ('windef.h', 'winbase.h', 'wingdi.h', 'winuser.h', 'winreg.h', 'mmsystem.h', 'winnt.h', 'excpt.h',
                 'ntstatus.h', 'winerror.h', 'winapifamily.h', 'psapi.h', 'winver.h'):
        write_if_changed(include / name, '/* Generated by tools/arm64_sdk.py. */\n#include <windows.h>\n')
    # The build number of the headers, which sources ask for to know what
    # they declare. These declare what Windows 98 has.
    write_if_changed(include / 'ntverp.h', '/* Generated by tools/arm64_sdk.py. */\n#include <windows.h>\n'
                     '#define VER_PRODUCTBUILD 10000\n')
    lld = tool('lld-link')
    for dll in sorted({f['dll'] for f in model.functions}):
        names = sorted({f['name'] for f in model.functions if f['dll'] == dll})
        definition = lib / (dll[:-4].lower() + '.def')
        definition.write_text(f'LIBRARY {dll}\nEXPORTS\n' + ''.join(f'    {n}\n' for n in names))
        subprocess.run([lld, '/lib', '/machine:arm64', f'/def:{definition}', f'/out:{lib / (dll[:-4].lower() + ".lib")}'],
                       check=True, capture_output=True)
    objects = []
    for source in sorted((ROOT / 'sdk/start').glob('*.[cS]')):
        obj = lib / (source.stem + '.obj')
        subprocess.run(['clang', *RUNTIME_CFLAGS, f'-I{include}', '-c', str(source), '-o', str(obj)], check=True)
        objects.append(obj)
    subprocess.run([lld, '/lib', '/machine:arm64', f'/out:{lib / "start.lib"}', *map(str, objects)],
                   check=True, capture_output=True)
    for obj in objects:
        obj.unlink()
    crt_library(include, lib, lld)
    # DirectX: ddraw.h and dsound.h, dxguid.lib, and the ARM64 forms of
    # DDRAW.DLL and DSOUND.DLL with their import libraries.
    import arm64_dx
    arm64_dx.headers(include)
    arm64_dx.libraries(SDK, CFLAGS, tool)
    stamp.write_text(identity)
    return SDK


def crt_library(include, lib, lld):
    """The static C runtime: the headers into the include directory, own
    sources of sdk/crt and the math functions of musl (third_party/musl-math,
    MIT, imported by tools/sdk_dependencies.py) into crt.lib; the helper
    library sdk/lib into neo98.lib."""
    from concurrent.futures import ThreadPoolExecutor
    import sdk_dependencies
    sdk_dependencies.ensure('musl-math')
    # The shim (sdk/crt/musl-shim) is for building the sources of musl and
    # never reaches a program.
    for path in [*CRT.glob('include/**/*.h'), MUSL / 'include/math.h']:
        base = CRT / 'include' if (CRT / 'include') in path.parents else MUSL / 'include'
        target = include / path.relative_to(base)
        target.parent.mkdir(parents=True, exist_ok=True)
        write_if_changed(target, path.read_text())
    work = lib / 'crt.obj.d'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    # The sources of musl and the layer below them see musl's internal headers
    # and, before the runtime's own headers, the shim that stands for the
    # rest of musl's tree. -fno-builtin stays: a compiler that knows printf
    # must not rewrite the printf it is compiling.
    internal = ['-include', 'features.h', f'-I{MUSL_SHIM}', f'-I{MUSL / "src"}', f'-I{include}']
    jobs = [(source, internal) for source in sorted((CRT / 'src').glob('*.[cS]'))]
    # musl's formatter under another name: printf of the Windows runtime knows
    # %I64d and prints %p its own way (sdk/crt/src/printf_windows.c).
    renamed = {'vfprintf.c': ['-Dvfprintf=__musl_vfprintf']}
    jobs += [(source, ['-w', *renamed.get(source.name, []), *internal]) for source in sorted((MUSL / 'src').glob('*.c'))]

    # Keep musl's decoder sources byte-for-byte unchanged. A private source
    # directory supplies the Windows wchar_t bound through their internal
    # header; the imported tree and its checksum manifest remain untouched.
    multibyte = work / 'multibyte'
    multibyte.mkdir()
    (multibyte / 'internal.h').write_text(
        f'#include "{MUSL / "src/internal.h"}"\n#include "multibyte_windows.h"\n')
    adapted = []
    for source, flags in jobs:
        if MUSL in source.parents and source.name in ('mbrtowc.c', 'mbtowc.c', 'mbsrtowcs.c'):
            copy = multibyte / source.name
            shutil.copyfile(source, copy)
            source = copy
        adapted.append((source, flags))
    jobs = adapted

    def compile_one(job):
        source, flags = job
        obj = work / (source.stem + ('.m' if MUSL in source.parents else '') + '.obj')
        subprocess.run(['clang', *RUNTIME_CFLAGS, *flags, '-c', str(source), '-o', str(obj)], check=True)
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_one, jobs))
    subprocess.run([lld, '/lib', '/machine:arm64', f'/out:{lib / "crt.lib"}', *map(str, objects)], check=True,
                   capture_output=True)
    helper = sorted((ROOT / 'sdk/lib').glob('*.c'))
    if helper:
        for path in (ROOT / 'sdk/lib').glob('*.h'):
            write_if_changed(include / path.name, path.read_text())
        # mpeg.c compiles pl_mpeg (MIT) as it is.
        mpeg = sdk_dependencies.ensure('pl_mpeg')
        objects = [compile_one((source, [f'-I{include}', f'-I{mpeg}', '-Wno-unused-function'])) for source in helper]
        subprocess.run([lld, '/lib', '/machine:arm64', f'/out:{lib / "neo98.lib"}', *map(str, objects)], check=True,
                       capture_output=True)
    shutil.rmtree(work, ignore_errors=True)


def cxx_library(sdk):
    """The C++ runtime: libc++, libc++abi and libunwind of LLVM (third_party,
    imported by tools/sdk_dependencies.py, unchanged) with sdk/cxx/threads.cpp
    in place of libc++'s thread file for Windows, into cxx.lib; their headers
    into include/c++/v1. Built when the first C++ program asks for it and
    again when its sources, the headers of the C runtime or the flags change;
    an object whose inputs did not change is kept."""
    import fcntl
    import json
    from concurrent.futures import ThreadPoolExecutor
    import sdk_dependencies
    with open(sdk / '.lock.cxx', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        roots = {name: sdk_dependencies.ensure(name) for name in ('libcxx', 'libcxxabi', 'libunwind', 'llvm-libc', 'compiler-rt')}
        tzdata = sdk_dependencies.ensure('tzdata')
        tz_share = sdk / 'share/tzdata'
        tz_share.mkdir(parents=True, exist_ok=True)
        # IANA's own Makefile pipeline produces tzdata.zi. libc++ consumes
        # this compact format, whose continuations contain no inline comments.
        # Keep both IANA scripts and the libc++ parser unchanged.
        regions = ('africa', 'antarctica', 'asia', 'australasia', 'europe', 'northamerica', 'southamerica', 'etcetera', 'factory', 'backward')
        tz_stamp = hashlib.sha256((tzdata / 'neo98-import.json').read_bytes()).hexdigest()
        stamp = tz_share / '.neo98-stamp'
        if not stamp.exists() or stamp.read_text() != tz_stamp:
            data = subprocess.run(['awk', '-v', 'DATAFORM=main', '-f', 'ziguard.awk', *regions],
                                  cwd=tzdata, env=dict(os.environ, LC_ALL="C"), check=True, capture_output=True, text=True).stdout
            data = subprocess.run(['awk', '-v', 'dataform=main', '-v', 'redo=posix_right',
                                   '-v', 'version=' + (tzdata / 'version').read_text().strip(),
                                   '-f', 'zishrink.awk'], cwd=tzdata, env=dict(os.environ, LC_ALL="C"), input=data,
                                  check=True, capture_output=True, text=True).stdout
            write_if_changed(tz_share / 'tzdata.zi', data)
            stamp.write_text(tz_stamp)
        for name in ('leap-seconds.list', 'LICENSE'):
            write_if_changed(tz_share / name, (tzdata / name).read_text())
        include, lib = sdk / 'include', sdk / 'lib'
        headers = include / 'c++/v1'
        imports = hashlib.sha256(b''.join((roots[name] / 'neo98-import.json').read_bytes() for name in sorted(roots)))
        for path in sorted([*CXX.glob('__*'), *CXX.glob('*.h')]):
            imports.update(path.read_bytes())
        header_stamp = headers / '.neo98-stamp'
        if not header_stamp.exists() or header_stamp.read_text() != imports.hexdigest():
            shutil.rmtree(headers, ignore_errors=True)
            shutil.copytree(roots['libcxx'] / 'include', headers,
                            ignore=shutil.ignore_patterns('CMakeLists.txt', '*.in', '*.modulemap'))
            for name in ('libcxxabi', 'libunwind'):
                shutil.copytree(roots[name] / 'include', headers, dirs_exist_ok=True,
                                ignore=shutil.ignore_patterns('CMakeLists.txt', 'mach-o'))
            shutil.copy(CXX / '__config_site', headers / '__config_site')
            shutil.copy(roots['libcxx'] / 'vendor/llvm/default_assertion_handler.in', headers / '__assertion_handler')
            header_stamp.write_text(imports.hexdigest())
        common = [GNU_TARGET, '-nostdlibinc', '-nostdinc++', '-funwind-tables', '-fno-stack-protector', '-O2', '-w',
                  '-DNDEBUG', *cxx_includes(sdk)]
        unwind = ['-D_LIBUNWIND_IS_NATIVE_ONLY', '-D_LIBUNWIND_HIDE_SYMBOLS', '-D_LIBUNWIND_HAS_NO_THREADS',
                  f'-I{roots["libunwind"] / "include"}']
        abi = ['-D_LIBCXXABI_BUILDING_LIBRARY', '-D_LIBCPP_BUILDING_LIBRARY', '-DLIBCXX_BUILDING_LIBCXXABI',
               '-D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS', f'-I{roots["libcxx"] / "src"}']
        std = ['-D_LIBCPP_BUILDING_LIBRARY', '-DLIBCXX_BUILDING_LIBCXXABI', '-D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS',
               '-DLIBC_NAMESPACE=__llvm_libc_common_utils', f'-I{roots["libcxx"] / "src"}', f'-I{roots["llvm-libc"]}']
        jobs = [('builtin', roots['compiler-rt'] / 'lib/builtins' / name, []) for name in ('divti3.c', 'udivmodti4.c', 'udivti3.c', 'umodti3.c')]
        for name in ('libunwind.cpp', 'Unwind-seh.cpp'):
            jobs.append(('unwind', roots['libunwind'] / 'src' / name, [LIBCXX_STANDARD, '-fno-exceptions', '-fno-rtti', *unwind]))
        for name in ('UnwindLevel1.c', 'UnwindLevel1-gcc-ext.c', 'UnwindRegistersSave.S', 'UnwindRegistersRestore.S'):
            jobs.append(('unwind', roots['libunwind'] / 'src' / name, unwind))
        for source in sorted((roots['libcxxabi'] / 'src').glob('*.cpp')):
            # cxa_noexception.cpp is the runtime of a build without exceptions.
            if source.name != 'cxa_noexception.cpp':
                jobs.append(('abi', source, [LIBCXX_STANDARD, *abi]))
        # Keep libc++'s algorithms unchanged. Only the Windows portability
        # header and its directory identity type differ for Windows 98.
        fs_work = lib / 'filesystem-source'
        fs_work.mkdir(exist_ok=True)
        for source in (roots['libcxx'] / 'src/filesystem').iterdir():
            if source.suffix not in ('.cpp', '.h'): continue
            text = source.read_text()
            if source.name == 'time_utils.h':
                old = 'unsigned char id[16]; // FILE_ID_INFO::FileId'
                assert old in text
                text = text.replace(old, old + '\n    char directory[MAX_PATH]; // Win98 has no directory handles')
                old = 'bool operator==(const FileIdStruct& other) const {'
                assert old in text
                text = text.replace(old, old + '\n      if (directory[0] || other.directory[0]) return lstrcmpiA(directory, other.directory) == 0;')
            if source.name == 'posix_compat.h':
                text = (CXX / 'filesystem_posix_compat.h').read_text()
            destination = fs_work / source.name
            if not destination.exists() or destination.read_text() != text: destination.write_text(text)
        # print.cpp is unchanged; Win98 writes through its ANSI console.
        left_out = {'new.cpp'}
        names_only = sorted(fs_work.glob('*.cpp'))
        for source in sorted((roots['libcxx'] / 'src').glob('*.cpp')) + sorted((roots['libcxx'] / 'src/ryu').glob('*.cpp')) + names_only:
            if source.name not in left_out:
                adapter = 'filesystem_win98_api.h' if source.parent == fs_work else 'print_win98_api.h' if source.name == 'print.cpp' else None
                jobs.append(('std', source, [LIBCXX_STANDARD, *std, *(['-include', str(CXX / adapter), '-I' + str(CXX)] if adapter else [])]))
        tz_work = lib / 'tzdb-source'
        tz_work.mkdir(exist_ok=True)
        tz_source = roots['libcxx'] / 'src/experimental'
        for name in ('chrono_exception.cpp', 'time_zone.cpp', 'tzdb.cpp', 'tzdb_list.cpp'):
            text = (tz_source / name).read_text()
            if name == 'tzdb.cpp':
                old = '''#if defined(__linux__)
  return "/usr/share/zoneinfo/";
#else
#  error "unknown path to the IANA Time Zone Database"
#endif'''
                assert old in text
                text = text.replace(old, '  return chrono::__neo98_tzdb_directory();')
                old = '''  // TODO TZDB Implement this on Windows.
  std::__throw_runtime_error("unknown time zone");'''
                assert old in text
                text = text.replace(old, '''  if (const char* name = std::getenv("TZ"))
    if (*name) return tzdb.locate_zone(name);
  std::__throw_runtime_error("set TZ to an IANA zone name on Windows 98");''')
            source = tz_work / name
            write_if_changed(source, text)
            jobs.append(('tzdb', source, [LIBCXX_STANDARD, *std, '-I' + str(tz_source), '-include', str(CXX / 'tzdb_win98.h')]))
        jobs.append(('std', roots['libcxx'] / 'src/support/win32/locale_win32.cpp', [LIBCXX_STANDARD, *std]))
        for source in sorted(CXX.glob('*.cpp')):
            jobs.append(('sdk', source, [LIBCXX_STANDARD, *std]))
        # Every header a source can see: one hash for all of them.
        seen = hashlib.sha256(imports.digest())
        for path in sorted(p for p in include.rglob('*') if p.is_file() and headers not in p.parents):
            seen.update(path.read_bytes())
        work = lib / 'cxx.obj.d'
        work.mkdir(exist_ok=True)

        def compile_one(job):
            group, source, flags = job
            obj = work / f'{group}-{source.stem}{source.suffix.replace(".", "-")}.obj'
            key = hashlib.sha256(seen.digest() + source.read_bytes() + repr((common, flags)).encode()).hexdigest()
            stamp = obj.with_suffix('.key')
            if obj.exists() and stamp.exists() and stamp.read_text() == key:
                return obj, False
            compiler = 'clang++' if source.suffix == '.cpp' else 'clang'
            subprocess.run([compiler, *common, *flags, '-c', str(source), '-o', str(obj)], check=True)
            stamp.write_text(key)
            return obj, True
        with ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(compile_one, jobs))
        library = lib / 'cxx.lib'
        wanted = {obj for obj, _ in results}
        for stale in set(work.glob('*.obj')) - wanted:
            stale.unlink()
        if any(changed for _, changed in results) or not library.exists():
            subprocess.run([tool('lld-link'), '/lib', '/machine:arm64', f'/out:{library}', *map(str, sorted(wanted))],
                           check=True, capture_output=True)
        return library


def compiler(sdk, source, target):
    """The command that compiles one source for a target ('msvc' or 'gnu'),
    without the source and the output: clang with the flags and the include
    directories of the SDK. -isystem: the headers of the SDK are no place
    for the warnings of the program."""
    if Path(source).suffix.lower() in ('.cpp', '.cc', '.cxx'):
        return ['clang++', *CXXFLAGS, *cxx_includes(sdk)]
    return ['clang', *(gnu(CFLAGS) if target == 'gnu' else CFLAGS), '-isystem', str(sdk / 'include')]


def linker(sdk, objects, output, gui=False, dll=False, entry=None, cxx=False):
    """The command that links objects into a program or a DLL."""
    libs = program_libraries(sdk, cxx)
    # The linker's MinGW mode, for every program. Objects for the MinGW
    # target (C++) need it: it keeps the unwind records of functions that
    # are their own sections and makes the list of static constructors
    # (.ctors). The C runtime needs it as well: musl gives a hook two weak
    # defaults in two files (the standard streams in fflush.c and
    # __stdio_exit.c), which the linker refuses in its other mode.
    kind = (['/dll', '/entry:_DllMainCRTStartup', f'/implib:{Path(output).with_suffix(".lib")}'] if dll else
            [f'/subsystem:{"windows" if gui else "console"}',
             f'/entry:{entry or ("WinMainCRTStartup" if gui else "mainCRTStartup")}'])
    return [tool('lld-link'), '/lldmingw', '/machine:arm64', '/nodefaultlib', *kind,
            *map(str, objects), *map(str, libs), f'/out:{output}']


def program_libraries(sdk, cxx=False):
    """The libraries every program is linked with, in link order."""
    # A library a program does not call leaves no import behind.
    return [sdk / 'lib' / 'start.lib', *sorted((sdk / 'lib').glob('neo98.lib')),
            *([sdk / 'lib' / 'cxx.lib'] if cxx else []), sdk / 'lib' / 'crt.lib',
            *sorted((sdk / 'lib').glob('*32.lib')), sdk / 'lib' / 'winmm.lib', sdk / 'lib' / 'neo98touch.lib',
            # DirectDraw and DirectSound: the import libraries of the ARM64
            # system DLLs and the GUIDs.
            *(sdk / 'lib' / name for name in ('ddraw.lib', 'dsound.lib', 'dxguid.lib'))]


def cmake_toolchain():
    """A toolchain file for CMake, build/sdk/cmake/neo98-arm64.cmake: the
    compiler, the flags, the headers and the libraries `build` uses, for a
    program whose build is a CMake project (thousands of sources, options
    per source, objects kept between builds). CMake sees clang for the MinGW
    target of ARM64, as a program with C++ sources is built here; the link
    goes through the driver of clang to the same linker in the same mode, so
    a project's `-Wl,--subsystem,windows` and `target_link_libraries(winmm)`
    mean what they mean in its x86 build. What a project sets itself
    (language standard, -O, warnings) is not in the file.
    docs/ARM64-APPS.md, "CMake"."""
    sdk = generate()
    cxx_library(sdk)
    folder = sdk / 'cmake'
    folder.mkdir(exist_ok=True)
    project = {'-O2', '-Wall', '-std=c++17'}
    target = GNU_TARGET.split('=', 1)[1]
    c_flags = [flag for flag in gnu(CFLAGS) if flag != GNU_TARGET and flag not in project] + ['-isystem', str(sdk / 'include')]
    cxx_flags = [flag for flag in CXXFLAGS if flag != GNU_TARGET and flag not in project] + cxx_includes(sdk)
    libraries = '[==[' + ' '.join(f'"{path}"' for path in program_libraries(sdk, cxx=True)) + ']==]'
    # ld.lld for a MinGW target is lld-link in its MinGW mode (/lldmingw),
    # the mode `linker` asks for. -nostdlib: no start files and no libraries
    # of a MinGW installation; the ones of the SDK follow the objects.
    link = f'--ld-path={tool("ld.lld")} -nostdlib -L{sdk / "lib"}'
    text = f"""# Generated by tools/arm64_sdk.py. Do not edit.
# cmake -DCMAKE_TOOLCHAIN_FILE=<this file>: a program for ARM64 Windows
# against the SDK of NEO98 (docs/ARM64-APPS.md).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(NEO98_SDK "{sdk}")
set(CMAKE_C_COMPILER "{tool('clang')}")
set(CMAKE_CXX_COMPILER "{tool('clang++')}")
set(CMAKE_C_COMPILER_TARGET {target})
set(CMAKE_CXX_COMPILER_TARGET {target})
set(CMAKE_AR "{tool('llvm-ar')}" CACHE FILEPATH "")
set(CMAKE_RANLIB "{tool('llvm-ranlib')}" CACHE FILEPATH "")
set(CMAKE_RC_COMPILER "{tool('llvm-windres')}" CACHE FILEPATH "")
set(CMAKE_RC_FLAGS_INIT [==[--target=aarch64-w64-mingw32 -I "{sdk / 'include'}"]==])
# No program of a compiler check is linked or run: the checks compile only.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS_INIT "{' '.join(c_flags)}")
set(CMAKE_CXX_FLAGS_INIT "{' '.join(cxx_flags)}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "{link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "{link}")
# After the libraries a project names: start code, C++ and C runtime, the
# import libraries of the system. FORCE, because the platform file of CMake
# puts the libraries of a MinGW installation here.
set(CMAKE_C_STANDARD_LIBRARIES {libraries} CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES {libraries} CACHE STRING "" FORCE)
set(CMAKE_FIND_ROOT_PATH "{sdk}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
"""
    path = folder / 'neo98-arm64.cmake'
    write_if_changed(path, text)
    return path


def build(sources, output, gui=False, extra_cflags=(), dll=False, entry=None, capture=None, target=None):
    """Compile and link one program, or one DLL, against the SDK. A source
    may be a `.lib` (the import library of a DLL built before) or a `.def`
    (the exports of the DLL). A DLL leaves its import library beside it.
    `entry` names the entry of a program that brings its own start code;
    `capture` is a file for what compiler and linker say. `target` is 'msvc'
    or 'gnu'; without it a program with C++ sources is built for the MinGW
    target and any other for the one of Microsoft's ABI."""
    sdk = generate()
    has_cxx = any(Path(source).suffix.lower() in ('.cpp', '.cc', '.cxx') for source in sources)
    if target is None:
        target = 'gnu' if has_cxx else 'msvc'
    if has_cxx and target != 'gnu':
        raise ApiError('C++ is built for the MinGW target only (docs/ARM64-APPS.md, "C++")')
    if has_cxx:
        cxx_library(sdk)
    output = Path(output)
    work = output.parent / (output.name + '.obj.d')
    work.mkdir(parents=True, exist_ok=True)
    objects = []
    stream = open(capture, 'w') if capture else None
    said = {'stdout': stream, 'stderr': subprocess.STDOUT} if stream else {}
    try:
        for source in map(Path, sources):
            if source.suffix.lower() in ('.lib', '.def'):
                objects.append(source if source.suffix.lower() == '.lib' else f'/def:{source}')
                continue
            if source.suffix.lower() == '.rc':
                # lld-link takes the.res itself. llvm-rc runs the preprocessor
                # of clang over the script, with RC_INVOKED defined.
                obj = work / (source.stem + '.res')
                # Dashes, and `--` before the input: llvm-rc takes an absolute
                # path that begins with a slash for an option otherwise.
                result = subprocess.run([tool('llvm-rc'), '-I', str(sdk / 'include'), '-I', str(source.parent),
                                         '-FO', str(obj), '--', str(source)], capture_output=True, text=True)
                if result.returncode:
                    raise ApiError(f'llvm-rc {source}:\n{result.stdout}{result.stderr}')
            else:
                obj = work / (source.stem + '.obj')
                # A language standard belongs to the sources of its language.
                is_cxx = source.suffix.lower() in ('.cpp', '.cc', '.cxx')
                own = [flag for flag in extra_cflags if not flag.startswith('-std=') or ('++' in flag) == is_cxx]
                subprocess.run([*compiler(sdk, source, target), *own, '-c', str(source), '-o', str(obj)],
                               check=True, **said)
            objects.append(obj)
        subprocess.run(linker(sdk, objects, output, gui=gui, dll=dll, entry=entry, cxx=has_cxx), check=True, **said)
    finally:
        if stream:
            stream.close()
        shutil.rmtree(work, ignore_errors=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sources', nargs='*', help='C sources and .rc files; none only generates the SDK')
    parser.add_argument('-o', '--output')
    parser.add_argument('--gui', action='store_true', help='subsystem Windows with WinMain instead of console with main')
    parser.add_argument('--dll', action='store_true', help='build a DLL; its import library is written beside it')
    parser.add_argument('--entry', help='the entry of a program that brings its own start code')
    parser.add_argument('--force', action='store_true', help='regenerate the SDK even when it is current')
    parser.add_argument('--target', choices=('msvc', 'gnu'),
                        help='the ABI: msvc for C (the default without C++), gnu for C++ and for C beside it')
    parser.add_argument('--cxx', action='store_true', help='with no sources: build the C++ runtime as well')
    parser.add_argument('--cmake-toolchain', action='store_true',
                        help='with no sources: print the toolchain file for a CMake project (builds the SDK with C++)')
    parser.add_argument('-D', dest='defines', action='append', default=[], help='preprocessor definition')
    parser.add_argument('-I', dest='includes', action='append', default=[], help='include directory')
    # What else begins with -std=, -f, -O or -W goes to the compiler as it
    # is (-std=c++20, -fwrapv, -fno-strict-aliasing): a build tool that calls
    # clang for another target calls this one with the same options.
    args, passed = parser.parse_known_args()
    refused = [flag for flag in passed if not flag.startswith(('-std=', '-f', '-O', '-W'))]
    if refused:
        parser.error('unknown arguments: ' + ' '.join(refused))
    try:
        if not args.sources and args.cmake_toolchain:
            if args.force:
                generate(True)
            print(cmake_toolchain())
            return 0
        if not args.sources:
            sdk = generate(args.force)
            if args.cxx:
                cxx_library(sdk)
            print(sdk)
            return 0
        if not args.output:
            parser.error('-o program.exe is required')
        extra = [f'-D{d}' for d in args.defines] + [f'-I{i}' for i in args.includes] + passed
        print(build(args.sources, args.output, args.gui, extra, args.dll, args.entry, target=args.target))
    except ApiError as error:
        print(error, file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        if error.stderr:
            sys.stderr.write(error.stderr if isinstance(error.stderr, str) else error.stderr.decode())
        return error.returncode or 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
