#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create local GDI comparison fixtures. Never call from a release build."""
import argparse
from pathlib import Path
import struct


def segment(data, number, offset, size):
    ne = struct.unpack_from('<I', data, 60)[0]
    table = ne + struct.unpack_from('<H', data, ne + 34)[0]
    shift = struct.unpack_from('<H', data, ne + 50)[0]
    start = (struct.unpack_from('<H', data, table + (number - 1) * 8)[0] << shift) + offset
    raw = data[start:start + size]
    if len(raw) != size: raise ValueError('Truncated GDI table')
    return raw


def generate(drive_c, output):
    drive_c, output = Path(drive_c), Path(output)
    data = (drive_c / 'WINDOWS/SYSTEM/GDI.EXE').read_bytes()
    text = '// Local comparison data from the supplied installation. Do not distribute.\n#pragma once\nnamespace Win98GdiPalette {\n'
    for name, number, offset, size in [('DefaultColors', 11, 0x25da, 80), ('HalftoneColors', 2, 0x118a, 1024)]:
        values = struct.unpack('<' + 'I' * (size // 4), segment(data, number, offset, size))
        text += 'inline const unsigned ' + name + '[]={' + ','.join(map(str, values)) + '};\n'
    text += '}\n'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'gdi_palette_tables.h').write_text(text)
    data = (drive_c / 'WINDOWS/SYSTEM/GDI32.DLL').read_bytes()
    pe = struct.unpack_from('<I', data, 60)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    text = '// Local comparison data. Do not distribute.\n#pragma once\nnamespace GdiFixture {\n'
    for name, address, length in [('Types', 0x4d95, 13), ('Offsets', 0x4790, 8)]:
        raw = None
        for i in range(count):
            at = pe + 24 + optional + i * 40
            rva, size, offset = struct.unpack_from('<III', data, at + 12)
            if rva <= address and address - rva + length <= size:
                raw = data[offset + address - rva:offset + address - rva + length]
        if raw is None or len(raw) != length: raise ValueError('Missing GDI32 table')
        text += 'inline const unsigned char ' + name + '[]={' + ','.join(map(str, raw)) + '};\n'
    (output / 'gdi_object_tables.h').write_text(text + '}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--drive-c', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/generated'))
    args = parser.parse_args()
    generate(args.drive_c, args.output)

if __name__ == '__main__': main()
