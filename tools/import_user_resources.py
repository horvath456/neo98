#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import the USER.EXE pieces the native window frame still lacked.

Two things come out of the local Win98 SE USER.EXE here.

The default application icon. A window whose class carries no icon still shows
one in its caption: USER falls back to its own icon resource, the little window
with the blue caption. It is GROUP_ICON 100 with a 32x32 and a 16x16 member; the
caption uses the 16x16 one, at SM_CXSMICON by SM_CYSMICON.

The OEM bitmap strip. USER does not keep the caption buttons, scroll arrows,
menu glyphs, check boxes and radio buttons as bitmap resources. It builds them
once into a memory bitmap and afterwards blits sub-rectangles out of it. The
table that says what each of the 91 strip entries is lives in USER's segment 21
at 0B87h: two words per entry, a DrawFrameControl type and a DrawFrameControl
state. The painter calls the native DrawFrameControl implementation, once per entry, and is the blit that copies one entry out of the
cache later. The strip is therefore a cache of DrawFrameControl output, not a
separate set of pixels.

The generated header carries the icon bits and the strip table; the provenance
file carries the hashes and the addresses.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

def u16(data, offset): return struct.unpack_from('<H', data, offset)[0]
def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]
from provenance import stamp as stamp_provenance      # noqa: E402

USER = ROOT / 'build/gdi-audit/USER.EXE'
OUT = ROOT / 'build/generated'
EXPECTED = '3d40896c26460c9c0c79ff165e703ddb1bbaec72bae7528c9a20ddd38dae41ef'

# USER segment 21, offset 0B87h: the DrawFrameControl call each strip entry is.
STRIP_SEGMENT, STRIP_OFFSET, STRIP_ENTRIES = 21, 0x0b87, 0x5b
# The icon a window without a class icon shows: USER loads resource
# 69h (105, the Windows logo, OIC_WINLOGO) at the small icon size, and
# Windows 98 takes it when neither the window nor its class has an
# icon. Not group 100, the sample window of IDI_APPLICATION.
DEFAULT_ICON_GROUP = '105'

RESOURCE_TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG',
                  6: 'STRING', 7: 'FONTDIR', 8: 'FONT', 9: 'ACCELERATOR',
                  10: 'RCDATA', 11: 'MESSAGETABLE', 12: 'GROUP_CURSOR',
                  14: 'GROUP_ICON', 16: 'VERSION'}


def resources(data):
    """Every resource of a 16-bit NE module, with its file span."""
    ne = u32(data, 0x3c)
    table = ne + u16(data, ne + 36)
    shift = u16(data, table)
    at = table + 2
    out = []
    while True:
        kind = u16(data, at)
        if kind == 0:
            return out
        count = u16(data, at + 2)
        at += 8
        for _ in range(count):
            offset, length, flags, name = struct.unpack_from('<4H', data, at)
            at += 12
            if name & 0x8000:
                label = str(name & 0x7fff)
            else:
                size = data[table + name]
                label = data[table + name + 1:table + name + 1 + size].decode('latin1')
            if kind & 0x8000:
                tag = RESOURCE_TYPES.get(kind & 0x7fff, str(kind & 0x7fff))
            else:
                size = data[table + kind]
                tag = data[table + kind + 1:table + kind + 1 + size].decode('latin1')
            out.append({'type': tag, 'name': label, 'flags': flags,
                        'offset': offset << shift, 'length': length << shift})


def icon_members(data, group):
    """The (width, height, resource id) members of one GROUP_ICON."""
    _reserved, _kind, count = struct.unpack_from('<3H', data, group['offset'])
    members = []
    for index in range(count):
        width, height, _colours, _r, _planes, _bits, size, ident = struct.unpack_from(
            '<4B2HIH', data, group['offset'] + 6 + index * 14)
        members.append({'width': width or 256, 'height': height or 256,
                        'id': str(ident), 'size': size})
    return members


def decode_icon(blob):
    """One ICON resource as (width, height, rows of (opaque, r, g, b))."""
    size, width, height, _planes, bits = struct.unpack_from('<IiiHH', blob, 0)
    height //= 2
    colours = struct.unpack_from('<I', blob, 32)[0] or (1 << bits)
    palette = [(blob[size + i * 4 + 2], blob[size + i * 4 + 1], blob[size + i * 4])
               for i in range(colours)]
    colour_base = size + colours * 4
    stride = ((width * bits + 31) // 32) * 4
    mask_stride = ((width + 31) // 32) * 4
    mask_base = colour_base + stride * height
    rows = []
    for y in range(height):
        source = colour_base + (height - 1 - y) * stride
        mask = mask_base + (height - 1 - y) * mask_stride
        row = []
        for x in range(width):
            if bits == 4:
                byte = blob[source + x // 2]
                index = (byte >> 4) if x % 2 == 0 else (byte & 15)
            elif bits == 8:
                index = blob[source + x]
            else:
                index = (blob[source + x // 8] >> (7 - x % 8)) & 1
            transparent = (blob[mask + x // 8] >> (7 - x % 8)) & 1
            red, green, blue = palette[index] if index < len(palette) else (0, 0, 0)
            row.append(0 if transparent else (0xff000000 | (red << 16) | (green << 8) | blue))
        rows.append(row)
    return width, height, rows


def strip_table(data):
    """The 91 (type, state) pairs the OEM bitmap strip builder walks."""
    base = u32(data, 0x3c)
    entry = base + u16(data, base + 0x22) + (STRIP_SEGMENT - 1) * 8
    sector, size = struct.unpack_from('<HH', data, entry)
    offset = sector << u16(data, base + 0x32)
    code = data[offset:offset + (size or 65536)]
    entries = []
    for index in range(STRIP_ENTRIES):
        kind, state = struct.unpack_from('<2H', code, STRIP_OFFSET + index * 4)
        entries.append((kind, state))
    raw = code[STRIP_OFFSET:STRIP_OFFSET + STRIP_ENTRIES * 4]
    return entries, hashlib.sha256(raw).hexdigest()


def header(icons, entries, destination):
    lines = [
        '// Original USER.EXE resources and tables, read from the local Windows 98 SE',
        '// installation. Do not redistribute.',
        '#pragma once',
        'namespace Win98UserResources {',
        '// A window whose class has no icon shows this one; USER keeps it as',
        '// GROUP_ICON 100. Rows are 0xAARRGGBB with alpha 0 for the mask holes.',
    ]
    for name, (width, height, rows) in sorted(icons.items()):
        lines.append('static const unsigned %s[%d * %d] = {' % (name, width, height))
        for row in rows:
            lines.append('    ' + ','.join('0x%08xu' % value for value in row) + ',')
        lines.append('};')
        lines.append('static const int %sWidth = %d, %sHeight = %d;'
                     % (name, width, name, height))
    lines += [
        '// USER segment 21 offset 0B87h. Each entry of the OEM bitmap strip is one',
        '// DrawFrameControl call: the type, then the state. Type 0xffff is a plain',
        '// fill through the colour-index brush table instead of a frame control.',
        'static const unsigned short StripCalls[%d][2] = {' % len(entries)]
    for kind, state in entries:
        lines.append('    {0x%04xu, 0x%04xu},' % (kind, state))
    lines += ['};',
              'static const unsigned StripCount = %d;' % len(entries),
              'inline int StripIndex(unsigned type, unsigned state) {',
              '    for (unsigned i = 0; i < StripCount; ++i)',
              '        if (StripCalls[i][0] == type && StripCalls[i][1] == state)',
              '            return (int)i;',
              '    return -1;}',
              '}']
    Path(destination).write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user', type=Path, default=USER)
    parser.add_argument('--out', type=Path, default=OUT)
    arguments = parser.parse_args()
    data = arguments.user.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED:
        raise SystemExit('Unexpected USER.EXE, SHA-256 %s' % digest)
    entries = resources(data)
    groups = {e['name']: e for e in entries if e['type'] == 'GROUP_ICON'}
    pictures = {e['name']: e for e in entries if e['type'] == 'ICON'}
    group = groups[DEFAULT_ICON_GROUP]
    members = icon_members(data, group)
    icons, sources = {}, []
    for member in members:
        picture = pictures[member['id']]
        blob = data[picture['offset']:picture['offset'] + picture['length']]
        width, height, rows = decode_icon(blob)
        name = 'DefaultIcon%d' % width
        icons[name] = (width, height, rows)
        sources.append({'symbol': name, 'resource': 'ICON %s' % member['id'],
                        'width': width, 'height': height,
                        'file_offset': picture['offset'], 'bytes': picture['length'],
                        'sha256': hashlib.sha256(blob).hexdigest()})
    calls, table_digest = strip_table(data)
    destination = arguments.out / 'user_resources.h'
    arguments.out.mkdir(parents=True, exist_ok=True)
    header(icons, calls, destination)
    record = {
        'module': {'file': 'USER.EXE', 'sha256': digest},
        'default_application_icon': {
            'group': 'GROUP_ICON %s' % DEFAULT_ICON_GROUP,
            'used_by': 'the caption of a window whose class has no icon; USER '
                       'and 6:38C0 resolve the small icon and DrawCaption 6:3454 '
                       'reserves the slot for it',
            'members': sources},
        'oem_bitmap_strip': {
            'table': '%d:%04x' % (STRIP_SEGMENT, STRIP_OFFSET),
            'entries': len(calls),
            'sha256': table_digest,
            'painter': 'USER calls once per entry into the memory DC '
                       'at USER data 016Eh',
            'sizes': 'USER derives each entry size from the same system '
                     'metrics DrawFrameControl is given',
            'blit': 'USER copies one entry out of the cache with SRCCOPY',
            'finding': 'the strip holds DrawFrameControl output, so the implemented '
                       'body produces the pixels the original blit source was made of'},
        'bitmap_resources': [
            {'name': e['name'], 'bytes': e['length'],
             'sha256': hashlib.sha256(
                 data[e['offset']:e['offset'] + e['length']]).hexdigest()}
            for e in entries if e['type'] == 'BITMAP'],
        'generated_sha256': {
            'user_resources.h': hashlib.sha256(destination.read_bytes()).hexdigest()}}
    stamp_provenance(record, 'user-resources-provenance.json')
    (arguments.out / 'user-resources-provenance.json').write_text(
        json.dumps(record, indent=2) + '\n')
    print('user_resources.h %s icons=%s strip=%d entries'
          % (record['generated_sha256']['user_resources.h'],
             ','.join(sorted(icons)), len(calls)))


if __name__ == '__main__':
    main()
