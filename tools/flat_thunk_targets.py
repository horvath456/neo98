#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Read the 16-bit target table a Win98 flat thunk block dispatches through.

The 32-bit thunk templates index a table of 16:16 addresses with a byte, so
the index alone does not say which routine a transition wants. This resolves
the table out of the local 16-bit NE module: it follows the thunk data export,
reads the far pointer the connector would have mapped, applies the segment
relocation chains and reports one row per index with the matching export
ordinal where the address is an entry point.

Reads the original file only. It implements nothing; it names what a recorded
transition index means.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
KIND = {0: "internal", 1: "import-ordinal", 2: "import-name", 3: "os-fixup"}


class Module:
    """The parts of a New Executable this tool needs."""

    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.header = self.word(0x3c)
        if self.data[self.header:self.header + 2] != b"NE":
            raise ValueError("Not a New Executable")
        self.align = self.word(self.header + 0x32) or 9
        self.segments = []
        table = self.header + self.word(self.header + 0x22)
        for index in range(self.word(self.header + 0x1c)):
            sector, length, flags, _ = struct.unpack("<HHHH", self.data[table + index * 8:table + index * 8 + 8])
            self.segments.append({"file": sector << self.align, "length": length, "flags": flags})
        self.entries = self._entries()
        self.names = self._names()

    def word(self, offset):
        return struct.unpack("<H", self.data[offset:offset + 2])[0]

    def _entries(self):
        start = self.header + self.word(self.header + 0x04)
        end = start + self.word(self.header + 0x06)
        entries, position, ordinal = {}, start, 1
        while position < end:
            count, kind = self.data[position], self.data[position + 1]
            position += 2
            if not count:
                break
            for _ in range(count):
                if kind == 0:
                    entries[ordinal] = None
                elif kind == 0xff:
                    entries[ordinal] = (self.data[position + 3], self.word(position + 4))
                    position += 6
                else:
                    entries[ordinal] = (kind, self.word(position + 1))
                    position += 3
                ordinal += 1
        return entries

    def _names(self):
        names = {}
        resident = self.header + self.word(self.header + 0x26)
        nonresident = struct.unpack("<I", self.data[self.header + 0x2c:self.header + 0x30])[0]
        for start in (resident, nonresident):
            position = start
            while self.data[position]:
                length = self.data[position]
                ordinal = self.word(position + 1 + length)
                names.setdefault(ordinal, self.data[position + 1:position + 1 + length].decode("latin-1"))
                position += length + 3
        return names

    def relocations(self, segment):
        """Resolve every segment fixup of one segment through its chains."""
        record = self.segments[segment - 1]
        if not record["flags"] & 0x0100:
            return {}
        base, length = record["file"], record["length"]
        table = base + length
        resolved = {}
        for index in range(self.word(table)):
            position = table + 2 + index * 8
            address_type, flags = self.data[position], self.data[position + 1]
            offset, first, second = (self.word(position + 2), self.word(position + 4),
                                     self.word(position + 6))
            current, seen = offset, set()
            while current != 0xffff and current not in seen and current + 2 <= length:
                seen.add(current)
                resolved[current] = {"address_type": address_type, "kind": KIND.get(flags & 3),
                                     "target": first, "value": second}
                current = self.word(base + current)
        return resolved

    def read(self, segment, offset, count):
        base = self.segments[segment - 1]["file"]
        return self.data[base + offset:base + offset + count]


def targets(module: Module, thunk_ordinal: int, count: int):
    segment, offset = module.entries[thunk_ordinal]
    block = module.read(segment, offset, 16)
    signature = block[:4].decode("latin-1")
    checksum = struct.unpack("<I", block[4:8])[0]
    pointer_offset = struct.unpack("<H", block[8:10])[0]
    fixups = module.relocations(segment)
    pointer_segment = fixups.get(offset + 10, {}).get("target")
    if pointer_segment is None:
        raise ValueError("The thunk data far pointer has no segment fixup")
    table_fixups = module.relocations(pointer_segment)
    reverse = {value: ordinal for ordinal, value in module.entries.items() if value}
    rows = []
    for index in range(count):
        position = pointer_offset + index * 4
        entry_offset = struct.unpack("<H", module.read(pointer_segment, position, 2))[0]
        entry_segment = table_fixups.get(position + 2, {}).get("target")
        ordinal = reverse.get((entry_segment, entry_offset)) if entry_segment else None
        rows.append({"index": index, "segment": entry_segment, "offset": entry_offset,
                     "export_ordinal": ordinal, "export_name": module.names.get(ordinal)})
    return {"signature": signature, "checksum": checksum,
            "table": {"segment": pointer_segment, "offset": pointer_offset}, "entries": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path, nargs="?", default=ROOT / "build/programs/SHELL.DLL")
    parser.add_argument("--thunk-ordinal", type=int, default=98,
                        help="ordinal of the 16-bit thunk data export, SHL3216_THUNKDATA16 for SHELL.DLL")
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--out", type=Path, default=ROOT / "build/stub-inventory/flat-thunk/targets.json")
    arguments = parser.parse_args()
    module = Module(arguments.module)
    report = targets(module, arguments.thunk_ordinal, arguments.count)
    report["module"] = str(arguments.module)
    report["sha256"] = hashlib.sha256(arguments.module.read_bytes()).hexdigest()
    report["thunk_data_export"] = module.names.get(arguments.thunk_ordinal)
    report["meaning"] = ("Index to 16-bit entry point for one flat thunk block. Read from the original "
                        "module; nothing here is implemented.")
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(json.dumps(report, indent=1) + "\n")
    print(f"{report['thunk_data_export']} {report['signature']} checksum {report['checksum']:#010x} "
          f"table {report['table']['segment']}:{report['table']['offset']:#06x}")
    for row in report["entries"]:
        name = row["export_name"] or ("internal" if row["segment"] else "unresolved")
        print(f"  index {row['index']:2d} -> {row['segment']}:{row['offset']:#06x}  {name}"
              + (f" (ordinal {row['export_ordinal']})" if row["export_ordinal"] else ""))
    print(f"Wrote {arguments.out}")


if __name__ == "__main__":
    main()
