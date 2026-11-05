#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Check the shared guest pages of a saved Windows state.

Guest pages that lie on bytes of the port (the bits of a DIB) are written as
a run and a pointer to their block. The CPU joins pages of blocks that lie
next to each other into one run; a load puts the blocks elsewhere, so every
run must fit into the block its pointer names. Runs into regions or variables are not checked.

  state_shared_pages.py STATE... exit 1 if a run reaches past its block
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from state_dump import Fields, Reader, parts  # noqa: E402


def mappings(chunks):
    """(process, guest address, bytes, reference bytes) of every shared run."""
    for name, payload in chunks:
        if name not in ('process', 'truetype'):
            continue
        reader = Reader(payload)
        reader.u64()  # the version of the part
        process = -1
        if name == 'process':
            process = reader.u64()
            reader.u64()  # the kind of processor
        while True:
            address, size, _prot, shared = (reader.u64() for _ in range(4))
            if not size:
                break
            if shared:
                yield process, address, size, reader.take(reader.u32())
                continue
            for _ in range(0, size, 1 << 20):
                offset = reader.u64()
                while offset != 0xffffffffffffffff:
                    reader.take(reader.u64())
                    offset = reader.u64()


def check(path):
    chunks = parts(Path(path).read_bytes())[1]
    fields = Fields(dict(chunks)['fields'])
    sizes = {record['name']: record['size'] for record in fields.types}
    inspected, bad, unchecked = 0, [], 0
    for process, address, size, reference in mappings(chunks):
        reader = Reader(reference)
        if reader.u8() != 1:  # not a block of the port
            unchecked += 1
            continue
        index, where = reader.u32(), reader.text()
        name, count, tail = fields.objects[index]
        element = sizes.get(name, 1) or 1
        extent = count * element + tail
        match = re.fullmatch(r'(?:\[(\d+)\])?(?:~(\d+))?', where)
        if not match:
            unchecked += 1
            continue
        offset = int(match[1] or 0) * element + (count * element + int(match[2]) if match[2] else 0)
        inspected += 1
        if offset > extent or size > extent - offset:
            bad.append(f'process {process} guest {address:08x}: {size} bytes on object {index} ({name}) '
                       f'at {offset} of {extent}')
    return inspected, unchecked, bad


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    failed = False
    for path in sys.argv[1:]:
        inspected, unchecked, bad = check(path)
        print(f'{path}: {inspected} shared runs on blocks checked, {unchecked} elsewhere, {len(bad)} too long')
        for line in bad:
            print('  ' + line)
        failed = failed or bool(bad)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
