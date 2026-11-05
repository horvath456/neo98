#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Copy the host test and application binaries out of the Win98 folder into build/programs.

`python3 neo98.py setup` runs this once. Every file an application profile of
tools/app_programs.py names is copied; one the folder does not have is removed
from build/programs and listed in build/programs/source.json, so no file of an
earlier installation stays behind.
"""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from app_programs import APPLICATIONS    # noqa: E402
import win98_c    # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROGRAMS = ROOT / "build/programs"
# Originals the tests read out of build/programs beside the profile files.
EXTRA = {name: f"::/WINDOWS/SYSTEM/{name}" for name in (
    "ADVAPI32.DLL", "COMMCTRL.DLL", "GDI32.DLL", "IMM32.DLL", "KERNEL32.DLL", "LZ32.DLL", "MFC30.DLL",
    "MFC42.DLL", "MSHTML.DLL", "MSTASK.DLL", "MSVCRT20.DLL", "OLEAUT32.DLL", "RICHED20.DLL", "RNAUI.DLL",
    "SHELL.DLL", "USER32.DLL", "VERSION.DLL", "WINMM.DLL", "WINSPOOL.DRV")}
EXTRA.update({name: f"::/WINDOWS/{name}" for name in ("EXTRAC32.EXE", "FONTVIEW.EXE", "WINIPCFG.EXE",
                                                       "WINVER.EXE")})


def sync(folder=None):
    folder = win98_c.require(Path(folder) if folder else win98_c.FOLDER)
    sources = dict(EXTRA)
    for entry in APPLICATIONS.values():
        if entry.get("program"):
            sources[entry["program"]] = entry["source"]
        for name, remote in (*entry.get("files", ()), *entry.get("regression_files", ())):
            sources[name] = remote
    PROGRAMS.mkdir(parents=True, exist_ok=True)
    copied, missing = 0, []
    for name, remote in sorted(sources.items()):
        source = win98_c.path(remote, folder)
        target = PROGRAMS / name
        if source is None or not source.is_file():
            target.unlink(missing_ok=True)
            missing.append(name)
            continue
        if not target.is_file() or source.read_bytes() != target.read_bytes():
            shutil.copyfile(source, target)
            copied += 1
    report = {"source": {"folder": str(folder.resolve())}, "files": len(sources) - len(missing),
              "copied": copied, "missing": missing}
    (PROGRAMS / "source.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    result = sync()
    print(f"Original programs from the Win98 folder: {result['files']} files, "
          f"{result['copied']} refreshed, {len(result['missing'])} absent"
          + (f" ({', '.join(result['missing'])})" if result["missing"] else ""))
