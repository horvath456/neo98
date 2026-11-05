#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reads a saved state of a machine with Windows programs without the host.
The stream describes itself, part by part and
field by field, and this tool needs nothing but the stream.

    build/analysis-venv/bin/python tools/state_dump.py FILE # the parts and their sizes
    build/analysis-venv/bin/python tools/state_dump.py FILE --fields # every value with its path
    build/analysis-venv/bin/python tools/state_dump.py A --compare B # the paths whose values differ

The comparison is the first guard of the format: a state that is loaded and
saved again at once must be the state that was loaded. A field that differs
is one the load did not put back, or one that holds something of the host
process as a number.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x4554415453383941  # "A98STATE"
FIELDS = 0x3130444C45494638  # "8FIELD01"
SCALAR, POINTER, FUNCTION, ARRAY, STRUCT, UNION, OPAQUE, VOID, VTABLE, ADDRESS = 0, 2, 3, 4, 5, 6, 7, 8, 9, 10
HOST, SKIP, COUNT, CUSTOM = 1, 2, 4, 8


class Reader:
    def __init__(self, data, at=0):
        self.data, self.at = data, at

    def take(self, count):
        if self.at + count > len(self.data):
            raise ValueError('the stream ends inside a value')
        self.at += count
        return self.data[self.at - count:self.at]

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack('<I', self.take(4))[0]

    def u64(self):
        return struct.unpack('<Q', self.take(8))[0]

    def text(self):
        return self.take(self.u32()).decode('utf-8', 'replace')


def parts(data):
    """(name, payload) of every part of a state, and its format number."""
    if data[:4] == b'A98Z':
        data = zlib.decompress(data[12:])
    reader = Reader(data)
    if reader.u64() != MAGIC:
        raise ValueError('not a state of a Windows machine')
    version = reader.u64()
    result = []
    while reader.at < len(data):
        name = reader.take(8).rstrip(b'\0').decode()
        result.append((name, reader.take(reader.u64())))
        if name == 'end':
            break
    return version, result


class Fields:
    """The part "fields": its types, and every value under its path."""

    def __init__(self, payload):
        reader = Reader(payload)
        head_bytes, body_bytes = reader.u64(), reader.u64()
        head = Reader(reader.take(head_bytes))
        self.body = Reader(reader.take(body_bytes))
        if head.u64() != FIELDS:
            raise ValueError('the fields are of another format')
        self.first_object = head.u32()
        self.types = []
        for _ in range(head.u32()):
            record = {'name': head.text(), 'kind': head.u32(), 'size': head.u32(), 'a': head.u32(), 'b': head.u32(),
                      'flat': head.u8() != 0, 'fields': []}
            for _ in range(head.u32()):
                record['fields'].append({'name': head.text(), 'type': head.u32(), 'offset': head.u32(),
                                         'bit_offset': head.u32(), 'bit_size': head.u32(), 'rule': head.u32()})
            self.types.append(record)
        self.values = {}
        self.objects = []
        self.read()

    def pointer(self, reader):
        tag = reader.u8()
        if tag == 0:
            return 'null'
        if tag == 1:
            return f'object {reader.u32()}{reader.text()}'
        if tag == 2:
            name, source, path = reader.text(), reader.text(), reader.text()
            return f'variable {name}@{source}{path}'
        if tag == 3:
            name, obj, offset = reader.text(), reader.text(), reader.u32()
            return f'symbol {name}@{obj}+{offset}'
        if tag == 4:
            return f'guest {reader.u32()}:{reader.u32():08x}'
        if tag == 5:
            return f'region {reader.text()}+{reader.u64():x}'
        if tag == 6:
            return f'string {reader.text()!r}'
        if tag == 7:
            return f'library {reader.text()}'
        if tag == 8:
            return f'number {reader.u64()}'
        raise ValueError(f'a pointer of no known kind ({tag})')

    def flat(self, kind, data, path):
        """The values inside bytes that were written as they are."""
        record = self.types[kind]
        if record['kind'] in (STRUCT, UNION) and record['fields']:
            for field in record['fields']:
                if field['bit_size']:
                    unit = int.from_bytes(data[field['bit_offset'] // 8:(field['bit_offset'] + field['bit_size'] + 7) // 8 + 1],
                                          'little')
                    self.values[f'{path}.{field["name"]}'] = (unit >> (field['bit_offset'] % 8)) & ((1 << field['bit_size']) - 1)
                else:
                    size = self.types[field['type']]['size']
                    self.flat(field['type'], data[field['offset']:field['offset'] + size], f'{path}.{field["name"]}')
        elif record['kind'] == ARRAY and self.types[record['a']]['kind'] in (STRUCT, UNION) and record['b'] <= 4096:
            size = self.types[record['a']]['size']
            for index in range(record['b']):
                self.flat(record['a'], data[index * size:(index + 1) * size], f'{path}[{index}]')
        else:
            self.values[path] = data if len(data) > 8 else int.from_bytes(data, 'little')

    def value(self, kind, path):
        record, reader = self.types[kind], self.body
        if record['flat']:
            self.flat(kind, reader.take(record['size']), path)
        elif record['kind'] == ARRAY:
            for index in range(record['b']):
                self.value(record['a'], f'{path}[{index}]')
        elif record['kind'] in (POINTER, FUNCTION, VTABLE, ADDRESS):
            self.values[path] = self.pointer(reader)
            if record['size'] > 8:
                self.values[path + '+8'] = reader.take(record['size'] - 8)
        elif record['kind'] == 11:  # a vector: how many, and where its elements are
            self.values[path + ' count'] = reader.u64()
            self.values[path] = self.pointer(reader)
        elif record['kind'] in (12, 13):  # a unique_ptr, a shared_ptr
            self.values[path] = self.pointer(reader)
        elif record['kind'] == 14:  # a map: its pairs in their order
            for index in range(reader.u64()):
                self.value(record['fields'][0]['type'], f'{path}{{{index}}}.key')
                self.value(record['fields'][1]['type'], f'{path}{{{index}}}.mapped')
        elif record['kind'] == 15:  # a string
            self.values[path] = reader.text()
        elif record['kind'] == STRUCT:
            for field in record['fields']:
                if field['rule'] in (HOST, SKIP, CUSTOM):
                    continue
                inner = f'{path}.{field["name"]}'
                if field['bit_size']:
                    self.values[inner] = reader.u64()
                elif self.types[field['type']]['kind'] == ARRAY and field['rule'] == COUNT:
                    for index in range(reader.u32()):
                        self.value(self.types[field['type']]['a'], f'{inner}[{index}]')
                else:
                    self.value(field['type'], inner)
        else:
            raise ValueError(f'a value of a kind that cannot be read: {record["name"]}')

    def read(self):
        reader = self.body
        for _ in range(reader.u32()):
            name, source, _symbol, kind, made = reader.text(), reader.text(), reader.text(), reader.u32(), reader.u8()
            self.values[f'{name}@{source} (made)'] = made
            self.value(kind, f'{name}@{source}')
        for index in range(reader.u32()):
            kind, count, tail, _aligned = reader.u32(), reader.u64(), reader.u64(), reader.u8()
            record = self.types[kind]
            self.objects.append((record['name'], count, tail))
            path = f'object {index} ({record["name"]})'
            self.values[path + ' count'] = count
            if record['flat'] and record['kind'] not in (STRUCT, UNION):
                self.values[path] = reader.take(count * record['size'])
            else:
                for element in range(count):
                    self.value(kind, f'{path}[{element}]' if count != 1 else path)
            if tail:
                self.values[path + ' tail'] = reader.take(tail)


def show(value):
    if isinstance(value, (bytes, bytearray)):
        return f'{len(value)} bytes {zlib.crc32(value):08x}'
    return str(value)


def compare(first, second, limit=40):
    """The paths whose values differ between two states, at most `limit`
    lines, and how many differ in all. Guest memory and registers are
    compared as parts."""
    lines, count = [], 0
    one, two = dict(parts(first)[1]), dict(parts(second)[1])
    for name in sorted(set(one) | set(two)):
        if name == 'fields' or one.get(name) == two.get(name):
            continue
        count += 1
        x, y = one.get(name, b''), two.get(name, b'')
        at = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
        lines.append(f'part {name}: {len(x)} and {len(y)} bytes differ from byte {at} on, '
                     f'in {sum(1 for i in range(min(len(x), len(y))) if x[i] != y[i])} bytes')
    a, b = Fields(one['fields']).values, Fields(two['fields']).values
    for path in list(a) + [path for path in b if path not in a]:
        if a.get(path) != b.get(path):
            count += 1
            if len(lines) < limit:
                lines.append(f'{path}: {show(a.get(path))} | {show(b.get(path))}')
    return count, lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('state', type=Path)
    parser.add_argument('--fields', action='store_true')
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--limit', type=int, default=40)
    arguments = parser.parse_args()
    data = arguments.state.read_bytes()
    if arguments.compare:
        count, lines = compare(data, arguments.compare.read_bytes(), arguments.limit)
        print('\n'.join(lines))
        print(f'{count} differ')
        return 1 if count else 0
    version, found = parts(data)
    print(f'format {version}')
    for name, payload in found:
        print(f'part {name}: {len(payload)} bytes')
    fields = Fields(dict(found)['fields'])
    print(f'fields: {len(fields.types)} types, {len(fields.objects)} objects, {len(fields.values)} values')
    if arguments.fields:
        for path, value in fields.values.items():
            print(f'{path} = {show(value)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
