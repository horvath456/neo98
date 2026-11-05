# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Read the two required COM base keys from the pinned local Win98 SYSTEM.DAT copy.

This diagnostic decodes its CREG/RGKN/RGDB records; it is not a general hive
validator or a native registry implementation.
"""
import hashlib
import json
from pathlib import Path
import struct

ROOT = next(p for p in Path(__file__).resolve().parents if (p/'neo98.py').exists())
HERE = ROOT/'build/ole-startup'
data = (HERE / 'SYSTEM.DAT').read_bytes()
digest = hashlib.sha256(data).hexdigest()
assert digest == '76fc202eacb94d3c28914bb77c4419d0e83df3ee295afcfb6051e0277b25b7c5'
assert data[:4] == b'CREG' and data[0x20:0x24] == b'RGKN'
first_block = struct.unpack_from('<I', data, 8)[0]
records = {}
block, index = first_block, 0
while block < len(data):
    assert data[block:block + 4] == b'RGDB'
    size = struct.unpack_from('<I', data, block + 4)[0]
    at, end = block + 32, block + size
    assert end <= len(data)
    while at < end:
        total, identifier, used, name_length, values = struct.unpack_from('<IIIHH', data, at)
        assert total >= 20 and at + total <= end
        if identifier != 0xffffffff and 20 + name_length <= used <= total:
            records[index << 16 | identifier] = {
                'name': data[at + 20:at + 20 + name_length].decode('cp1252'),
                'offset': at, 'used': used, 'name_length': name_length, 'value_count': values}
        at += total
    block, index = end, index + 1


def path(offset):
    parts, seen = [], set()
    while offset != 0x40:
        assert offset not in seen and 0x40 <= offset < first_block and len(seen) < 128
        seen.add(offset)
        words = struct.unpack_from('<7I', data, offset)
        parts.append(records[words[6]]['name'])
        offset = 0x20 + words[3]
    return '\\'.join(reversed(parts))



import sys
rows=[]
for offset in range(0x40,first_block-27,28):
    words=struct.unpack_from('<7I',data,offset)
    if words[0] or words[6] not in records:continue
    try:name=path(offset)
    except (AssertionError,KeyError,struct.error):continue
    if 'explorer' in name.lower() and ('performance' in name.lower() or name.lower().endswith('explorer')):rows.append(name)
report={'system_dat_sha256':digest,'matching_paths':rows,'performance_key_present':any(n.lower().endswith('explorer\\performance')for n in rows)}
Path(sys.argv[1]).write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
