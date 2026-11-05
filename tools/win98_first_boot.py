#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What Windows 98 puts back at the end of its first boot, for a folder of setup.

On the first boot after the copy, SETUPX.DLL `CfgClean1stBoot /CLEAN` works
through `[Reg1stBootClean]` of MSDOS.INF ("lines to remove from the registry
during 1st boot"): it saves each listed value under
`HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Setup\\Reg1stBootClean\\
<root>:<key>:<value>` (RootKey, SubKey, ValueName, DataString) and deletes it.
Among them are the `.Current` sounds of SystemStart, SystemExit, Open and Close
("disable sounds for startup and close program"), so the boots of setup stay
quiet. The RunOnce entry `RestoreCfgAuto` of MSDOS.INF,
`RUNDLL.EXE SETUPX.DLL,CfgClean1stBoot /RESTORE msdos.inf`, puts them back
on the same boot: enumerates the saved keys, creates
RootKey\\SubKey and sets ValueName to DataString as REG_SZ, then the saved key
is deleted, and the parent key once it is empty.

In the folder `setup` copies out of QEMU the restore never took effect: the
installation ends from the same RunOnce pass (NEO98Finish, tools/setup_win98.py),
the four saved keys are still in SYSTEM.DAT, and `SystemStart\\.Current` has no
value, so Windows started without "The Microsoft Sound". This module
writes what /RESTORE writes, as records of the registry journal beside the
folder; the hives stay as setup left them, like the time zone of
tools/win98_time_zone.py.

    build/analysis-venv/bin/python tools/win98_first_boot.py [--folder FOLDER] [--show]

Without --show it appends the records to the journal of the folder, once (the
saved keys are gone afterwards). Run it with NEO98 closed.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import import_registry  # noqa: E402
import win98_c  # noqa: E402
import win98_folder  # noqa: E402

SAVED = "Software\\Microsoft\\Windows\\CurrentVersion\\Setup\\Reg1stBootClean"
# turns the RootKey text into a handle. HKCU of Win98 without user
# profiles is HKU\.Default, the key the journal names for it.
ROOTS = {"HKLM": "HKEY_LOCAL_MACHINE", "HKCU": "HKEY_USERS\\.Default", "HKU": "HKEY_USERS",
         "HKCR": "HKEY_LOCAL_MACHINE\\Software\\Classes"}
REG_SZ = 1


def saved(folder):
    """The saved values of the folder's SYSTEM.DAT, in the order SETUPX
    enumerates them: (saved key, root, key, value name, data)."""
    hive = import_registry.parse(Path(folder) / "WINDOWS/SYSTEM.DAT")["tree"]
    parent = SAVED.lower()
    out = []
    for lower, (full, name, path, values) in hive.items():
        if path.lower() != parent:
            continue
        fields = {value.lower(): payload.rstrip(b"\0").decode("cp1252") for value, kind, payload in values}
        root = ROOTS.get(fields.get("rootkey", "").upper())
        if root is None or "subkey" not in fields:
            continue
        out.append((full, root, fields["subkey"], fields.get("valuename", ""), fields.get("datastring", "")))
    return out


def records(folder):
    """The journal records of CfgClean1stBoot /RESTORE for the folder."""
    rows = saved(folder)
    out = []
    for key, root, subkey, name, data in rows:
        path = root + "\\" + subkey
        raw = data.encode("cp1252") + b"\0"
        out.append(win98_folder.journal_record(1, path))
        out.append(win98_folder.journal_record(3, path, name) + struct.pack("<II", REG_SZ, len(raw)) + raw)
        out.append(win98_folder.journal_record(2, "HKEY_LOCAL_MACHINE\\" + key))
    if rows:
        out.append(win98_folder.journal_record(2, "HKEY_LOCAL_MACHINE\\" + SAVED))
    return out


def applied(journal):
    """Whether the journal already deletes the key of the saved values."""
    journal = Path(journal)
    if not journal.is_file():
        return False
    body = journal.read_bytes()[len(win98_folder.JOURNAL_SIGNATURE):]
    target = ("HKEY_LOCAL_MACHINE\\" + SAVED).upper()
    return any(kind == 2 and path.upper() == target for kind, path, _ in win98_folder.journal_walk(body))


def apply(folder, journal=None):
    """Append the restore to the journal beside `folder`, once. Returns the
    number of values restored."""
    journal = Path(journal or win98_c.journal(folder))
    if applied(journal):
        return 0
    rows = records(folder)
    if not rows:
        return 0
    existing = journal.read_bytes() if journal.is_file() else b""
    if not existing.startswith(win98_folder.JOURNAL_SIGNATURE):
        existing = win98_folder.JOURNAL_SIGNATURE
    journal.parent.mkdir(parents=True, exist_ok=True)
    journal.write_bytes(existing + b"".join(rows))
    return len(saved(folder))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--folder", type=Path, default=win98_c.FOLDER)
    parser.add_argument("--show", action="store_true", help="only print the saved values")
    args = parser.parse_args(argv)
    if args.show:
        for key, root, subkey, name, data in saved(args.folder):
            print(f"{root}\\{subkey} [{name or '(Standard)'}] = {data!r}")
        return 0
    win98_c.require(args.folder)
    journal = win98_c.journal(args.folder)
    restored = apply(args.folder, journal)
    # The autostart entries the cleanup removes stay removed after the
    # restore (SchedulingAgent is one of the saved values).
    if restored:
        win98_folder.registry_overlay(args.folder, journal)
    print(f"Restored {restored} first-boot values to {journal}" if restored
          else f"Nothing to restore: {journal} already contains the values or SYSTEM.DAT has no saved values")
    return 0


if __name__ == "__main__":
    sys.exit(main())
