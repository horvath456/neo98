#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The cabinets of the Windows 98 CD on drive C:, as on a preinstalled PC.

Windows 98 installs drivers and components out of the cabinet files of the
CD's WIN98 folder: the Add Printer wizard, Add/Remove Programs > Windows Setup,
the hardware wizard. SETUPX looks there first through the source path of
setup, HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Setup "SourcePath",
and asks for the CD when the files are not there ("Insert Windows 98 Second
Edition CD-ROM"). A computer maker's preinstallation copies the
WIN98 folder to C:\\WINDOWS\\OPTIONS\\CABS and points SourcePath at it; this
step does the same with the checked ISO of `neo98.py setup`, so no prompt for
the CD comes. The value goes into the registry change journal beside the
folder, the layer the runtime replays over the imported hives, and the step
notes itself in the done-marker so it runs once. README.md.

    build/analysis-venv/bin/python tools/win98_cabs.py --iso ISO [--folder FOLDER]
"""
from pathlib import Path
import argparse
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
import win98_folder  # noqa: E402
import win98_iso  # noqa: E402

TARGET = ("WINDOWS", "OPTIONS", "CABS")
SOURCE = "C:\\WINDOWS\\OPTIONS\\CABS"
KEY = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Setup"
REG_SZ = 1
SECTOR = 2048
VERSION = 1


def iso_files(iso):
    """(name, offset, size) of every file in the ISO's WIN98 folder."""
    return [(name, lba * SECTOR, size)
            for name, (lba, size, flags) in win98_iso.setup_files(iso).items()
            if not flags & 2]


def copy(iso, folder):
    """Copy the WIN98 folder of `iso` to WINDOWS\\OPTIONS\\CABS of `folder`;
    an identical file stays. Returns (files, bytes)."""
    target = Path(folder).joinpath(*TARGET)
    target.mkdir(parents=True, exist_ok=True)
    files = total = 0
    with Path(iso).open("rb") as image:
        for name, offset, size in iso_files(iso):
            path = target / name
            files += 1
            total += size
            if path.is_file() and path.stat().st_size == size:
                image.seek(offset)
                with path.open("rb") as existing:
                    left = size
                    while left:
                        chunk = image.read(min(left, 1 << 20))
                        if not chunk or chunk != existing.read(len(chunk)):
                            break
                        left -= len(chunk)
                    else:
                        continue
            image.seek(offset)
            with path.open("wb") as out:
                left = size
                while left:
                    chunk = image.read(min(left, 1 << 20))
                    if not chunk:
                        raise RuntimeError(f"{iso} ends inside WIN98\\{name}")
                    out.write(chunk)
                    left -= len(chunk)
    return files, total


def record():
    data = (SOURCE + "\\").encode("cp1252") + b"\0"
    return win98_folder.journal_record(3, KEY, "SourcePath") + struct.pack("<II", REG_SZ, len(data)) + data


def append(journal):
    journal = Path(journal)
    existing = journal.read_bytes() if journal.is_file() else b""
    if not existing.startswith(win98_folder.JOURNAL_SIGNATURE):
        existing = win98_folder.JOURNAL_SIGNATURE
    journal.parent.mkdir(parents=True, exist_ok=True)
    journal.write_bytes(existing + record())


def apply(iso, folder, journal=None):
    """Copy the cabinets and set SourcePath, once per folder. Returns
    (files, bytes) when it did something, None when it was done before."""
    folder = Path(folder)
    marker = folder / win98_c.MARKER
    info = json.loads(marker.read_text()) if marker.is_file() else None
    if info is not None and info.get("cabs") == VERSION:
        return None
    result = copy(iso, folder)
    append(journal or win98_c.journal(folder))
    if info is not None:
        info["cabs"] = VERSION
        marker.write_text(json.dumps(info, indent=2) + "\n")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--folder", type=Path, default=win98_c.FOLDER)
    args = parser.parse_args(argv)
    win98_c.require(args.folder)
    import setup_win98
    setup_win98.check_source_iso(args.folder, setup_win98.check_iso(args.iso))
    result = apply(args.iso, args.folder)
    if result:
        print(f"Cabinet files copied to {SOURCE} ({result[0]} files, {result[1] // 1048576} MB), "
              f"SourcePath recorded in {win98_c.journal(args.folder)}")
    else:
        print("Cabinet files are already registered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
