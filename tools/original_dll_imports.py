#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Which exports of the natively provided DLLs the original DLLs of the tree bind.

The import census measures 20 seconds of a program start. That misses
everything the shell, the codecs and the control panels reach only after a
click, and those callers hit the desktop: one missing export in MSHTML's path
stops the Explorer session (loader and GetRandomRgn failures
2026). This tool answers the other question, statically and in a second: for
every PE of the installed system tree, which of its imports are exports that
the port still leaves unimplemented.

It reads the classes from build/export-contract/report.json, so run
tools/export_contract.py first.

    build/analysis-venv/bin/python tools/original_dll_imports.py
    build/analysis-venv/bin/python tools/original_dll_imports.py --importer MSHTML.DLL
"""
from __future__ import annotations

import argparse
import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREE = ROOT / "build/win98-root/WINDOWS/SYSTEM"
CONTRACT = ROOT / "build/export-contract/report.json"
SUFFIXES = (".DLL", ".EXE", ".OCX", ".CPL", ".DRV", ".AX")


def fresh(report):
    """False when the report was written from other sources than this worktree.

    build/export-contract is a symlink into the main build directory in every
    worktree, so the report there belongs to whichever branch ran the tool last. Its own record of the source hashes tells the two apart.
    """
    import hashlib
    stamped = report.get("source_sha256", {}).get("src/x86_app.cpp")
    if not stamped:
        return True
    here = hashlib.sha256((ROOT / "src/x86_app.cpp").read_bytes()).hexdigest()
    return stamped == here


def classes(report):
    """The class of every export, by DLL and name, ordinals included."""
    out = {}
    for module in report["modules"]:
        dll = module["dll"].upper()
        for row in module["exports"]:
            for name in row["names"] or []:
                out[(dll, name)] = row["kind"]
            out[(dll, "ordinal_%d" % row["ordinal"])] = row["kind"]
    return out


def scan(tree, kinds, wanted=None):
    """Every (dll, export) of class missing that a PE of the tree imports."""
    import pefile
    bound = collections.defaultdict(set)
    files = 0
    for path in sorted(tree.rglob("*")):
        if path.suffix.upper() not in SUFFIXES or not path.is_file():
            continue
        if wanted and path.name.upper() != wanted.upper():
            continue
        files += 1
        try:
            image = pefile.PE(str(path), fast_load=True)
            image.parse_data_directories(
                directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        except Exception:
            continue
        for module in getattr(image, "DIRECTORY_ENTRY_IMPORT", []):
            dll = module.dll.decode("latin1").upper()
            for entry in module.imports:
                name = entry.name.decode("latin1") if entry.name else "ordinal_%d" % entry.ordinal
                if kinds.get((dll, name)) == "missing":
                    bound[(dll, name)].add(path.name)
        image.close()
    return bound, files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--tree", type=Path, default=TREE)
    parser.add_argument("--report", type=Path, default=CONTRACT)
    parser.add_argument("--importer", help="only this file of the tree")
    parser.add_argument("--json", type=Path, help="write the result here as well")
    args = parser.parse_args(argv)
    if not args.report.exists():
        print("run tools/export_contract.py first: no " + str(args.report), file=sys.stderr)
        return 2
    report = json.loads(args.report.read_text())
    if not fresh(report):
        print("stale: " + str(args.report) + " was written from other sources than this "
              "worktree. Run tools/export_contract.py again.", file=sys.stderr)
        return 2
    kinds = classes(report)
    bound, files = scan(args.tree, kinds, args.importer)
    rows = sorted(bound.items(), key=lambda item: (-len(item[1]), item[0]))
    print(f"{files} files of {args.tree}, {len(rows)} unimplemented exports bound")
    for (dll, name), importers in rows:
        print("%-13s %-27s %2d  %s" % (dll, name, len(importers),
                                       ", ".join(sorted(importers)[:8])))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(
            {"files": files,
             "exports": [{"dll": dll, "export": name, "importers": sorted(who)}
                         for (dll, name), who in rows]}, indent=1, ensure_ascii=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
