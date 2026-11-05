#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The registry of the Glide driver's setup, done after the installation.

A 3dfx card came with its own setup program. Besides the driver files it
registered a page of the display properties: a CLSID with its in-process
server, and under `Controls Folder\\Display\\shellex\\PropertySheetHandlers`
a handler that names the CLSID, where DESK.CPL looks for the pages of
others. The driver's own settings live under
`HKLM\\System\\CurrentControlSet\\Services\\3Dfx\\Device0\\glide`.

On NEO98 the driver GLIDE2X.DLL and the page GLIDEPAG.DLL are native
(src/x86_glide.cpp, src/x86_glide_page.inc); this step writes what their
setup would have written, as records of the registry change journal beside
the folder (src/registry_persist.cpp), like the time zone step
(tools/win98_time_zone.py). The settings key gets the two values with
their defaults: NEO98_SCALE 0 (automatic) and NEO98_ANTIALIAS 0.


    build/analysis-venv/bin/python tools/win98_glide_driver.py [--journal FILE]
"""
from pathlib import Path
import argparse
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
import win98_folder  # noqa: E402

# The class of the page; the same bytes as GlidePageClass in
# src/x86_glide_page.inc.
CLASS = "{5B0D7C3E-9A41-4F26-8E13-3DF6A2C98B51}"
MACHINE = "HKEY_LOCAL_MACHINE"
SETTINGS = "System\\CurrentControlSet\\Services\\3Dfx\\Device0\\glide"
REG_SZ = 1
VERSION = 1


def values(scale="0", antialias="0"):
    """(key, value name, data) of the driver's setup."""
    server = f"{MACHINE}\\Software\\Classes\\CLSID\\{CLASS}"
    handler = (f"{MACHINE}\\Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Display"
               "\\shellex\\PropertySheetHandlers\\Glide")
    return [(server, "", "Glide"),
            (server + "\\InprocServer32", "", "GLIDEPAG.DLL"),
            (server + "\\InprocServer32", "ThreadingModel", "Apartment"),
            (handler, "", CLASS),
            (f"{MACHINE}\\{SETTINGS}", "NEO98_SCALE", scale),
            (f"{MACHINE}\\{SETTINGS}", "NEO98_ANTIALIAS", antialias)]


def records(**settings):
    out = b""
    for key, name, text in values(**settings):
        data = text.encode("cp1252") + b"\0"
        out += win98_folder.journal_record(3, key, name) + struct.pack("<II", REG_SZ, len(data)) + data
    return out


def append(journal, **settings):
    """Append the records after everything the guest wrote so far; a test
    may give other settings than the defaults."""
    journal = Path(journal)
    existing = journal.read_bytes() if journal.is_file() else b""
    if not existing.startswith(win98_folder.JOURNAL_SIGNATURE):
        existing = win98_folder.JOURNAL_SIGNATURE
    journal.parent.mkdir(parents=True, exist_ok=True)
    journal.write_bytes(existing + records(**settings))


def apply(folder, journal=None):
    """Write the records into the journal beside `folder` and note it in the
    done-marker, once. Returns True when it wrote something."""
    folder = Path(folder)
    marker = folder / win98_c.MARKER
    info = json.loads(marker.read_text()) if marker.is_file() else None
    if info is not None and info.get("glide_driver") == VERSION:
        return False
    append(journal or win98_c.journal(folder))
    if info is not None:
        info["glide_driver"] = VERSION
        marker.write_text(json.dumps(info, indent=2) + "\n")
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--folder", type=Path, default=win98_c.FOLDER)
    parser.add_argument("--journal", type=Path, help="write the records into this journal only")
    args = parser.parse_args(argv)
    if args.journal:
        append(args.journal)
        return 0
    win98_c.require(args.folder)
    if apply(args.folder):
        print(f"Glide driver recorded in {win98_c.journal(args.folder)}")
    else:
        print("Glide driver is already registered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
