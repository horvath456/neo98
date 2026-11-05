#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The time zone step of Windows 98 Setup, done after the installation.

Setup reads `TimeZone` from `[Setup]` of MSBATCH.INF
and turns the first-boot entry of `RunOnce\\Setup`,
`RUNDLL32.EXE SHELL32.DLL,Control_RunDLL TIMEDATE.CPL,,/f` (SETUPC.INF line
214), into `...TIMEDATE.CPL,,/Z <zone>` in `RunOnce`. TIMEDATE.CPL then takes
the zone out of `HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Time
Zones\\<zone>` and calls SetTimeZoneInformation. On the installation of
`neo98.py setup` the `/f` entry ran instead (C:\\FRUNLOG.TXT), the
Date/Time page kept its default and the hive holds Pacific time.

This module writes what that SetTimeZoneInformation call writes: the values of
KERNEL32 (src/win98_timezone.h, Set) in their order, then the
DaylightFlag and ActiveTimeBias of its classification for the current date.
The data comes out of the installed Time Zones key; nothing is invented. The
records go into the registry change journal beside the folder, the layer the
runtime replays over the imported hives (src/registry_persist.cpp), like any
change the guest makes itself. README.md.

    build/analysis-venv/bin/python tools/win98_time_zone.py [--zone "W. Europe"] [--show]
"""
from pathlib import Path
import argparse
import calendar
import datetime
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402
import win98_folder  # noqa: E402

# The zone of the answer file (tools/setup_win98.py, answer_file).
ZONE = "W. Europe"
KEY = "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\TimeZoneInformation"
ZONES = "Software\\Microsoft\\Windows\\CurrentVersion\\Time Zones"
REG_SZ, REG_BINARY = 1, 3


def installed_zone(folder, zone=ZONE):
    """Std, Dlt and TZI of one zone of the installed SYSTEM.DAT."""
    import import_registry
    hive = win98_c.path("::/WINDOWS/SYSTEM.DAT", Path(folder))
    if hive is None:
        raise SystemExit(f"{folder} has no WINDOWS/SYSTEM.DAT")
    tree = import_registry.parse(hive)["tree"]
    entry = tree.get(f"{ZONES}\\{zone}".lower())
    if entry is None:
        raise SystemExit(f"The installation has no time zone {zone!r} under {ZONES}")
    values = {name.lower(): payload for name, kind, payload in entry[3]}
    tzi = values["tzi"]
    if len(tzi) != 44:
        raise SystemExit(f"TZI of {zone!r} has {len(tzi)} bytes, not 44")
    return values["std"].rstrip(b"\0"), values["dlt"].rstrip(b"\0"), tzi


def stored_active_bias(folder=None):
    """ActiveTimeBias in minutes as SYSTEM.DAT of the folder holds it: the bias
    KERNEL32 starts with before anything reclassifies the date."""
    import import_registry
    folder = Path(folder or win98_c.FOLDER)
    tree = import_registry.parse(win98_c.path("C:\\WINDOWS\\SYSTEM.DAT", folder) or folder / "WINDOWS/SYSTEM.DAT")["tree"]
    for key, (_, _, _, values) in tree.items():
        if key.endswith("\\control\\timezoneinformation"):
            for name, kind, data in values:
                if name == "ActiveTimeBias" and len(data) == 4:
                    return struct.unpack("<i", data)[0]
    raise SystemExit(f"{folder}: no ActiveTimeBias in SYSTEM.DAT")


def transition(rule, year):
    """The local moment of a relative SYSTEMTIME rule in `year`
    (, src/win98_timezone.h Transition): day 1..5 is the week, 5 the
    last one that still lies in the month."""
    rule_year, month, weekday, week, hour, minute, second, _ = struct.unpack("<8H", rule)
    if rule_year:
        return datetime.datetime(rule_year, month, week, hour, minute, second)
    first = datetime.date(year, month, 1)
    day = 1 + (weekday - (first.weekday() + 1) % 7) % 7
    last = calendar.monthrange(year, month)[1]
    for _ in range(1, week):
        if day + 7 > last:
            break
        day += 7
    return datetime.datetime(year, month, day, hour, minute, second)


def classify(standard, daylight, now):
    """1 standard time, 2 daylight time (, Classify)."""
    if not struct.unpack_from("<H", standard, 2)[0] or not struct.unpack_from("<H", daylight, 2)[0]:
        return 1
    std, dlt = transition(standard, now.year), transition(daylight, now.year)
    if std >= dlt:
        return 2 if dlt <= now < std else 1
    return 2 if now < std or now >= dlt else 1


def values(folder, zone=ZONE, now=None):
    """(name, type, data) in the order KERNEL32 writes them."""
    std_name, dlt_name, tzi = installed_zone(folder, zone)
    bias, standard_bias, daylight_bias = struct.unpack_from("<iii", tzi)
    standard, daylight = tzi[12:28], tzi[28:44]
    now = now or datetime.datetime.now()
    flag = classify(standard, daylight, now)
    active = bias + (daylight_bias if flag == 2 else standard_bias)

    def name(text):
        # SetTimeZoneInformation writes the ANSI name into a 33-byte buffer.
        return text[:32].ljust(33, b"\0")

    return [("Bias", REG_BINARY, struct.pack("<i", bias)),
            ("StandardBias", REG_BINARY, struct.pack("<i", standard_bias)),
            ("DaylightBias", REG_BINARY, struct.pack("<i", daylight_bias)),
            ("StandardStart", REG_BINARY, standard),
            ("DaylightStart", REG_BINARY, daylight),
            ("DaylightName", REG_SZ, name(dlt_name)),
            ("StandardName", REG_SZ, name(std_name)),
            ("DaylightFlag", REG_BINARY, struct.pack("<I", flag)),
            ("ActiveTimeBias", REG_BINARY, struct.pack("<i", active))]


def record(name, kind, data):
    """A `set value` record of the change journal (kind 3)."""
    return (win98_folder.journal_record(3, KEY, name)
            + struct.pack("<II", kind, len(data)) + data)


def append(journal, rows):
    """Append the records after everything the guest wrote so far; the last
    record of a value wins when the journal is replayed."""
    journal = Path(journal)
    existing = journal.read_bytes() if journal.is_file() else b""
    if not existing.startswith(win98_folder.JOURNAL_SIGNATURE):
        existing = win98_folder.JOURNAL_SIGNATURE
    journal.parent.mkdir(parents=True, exist_ok=True)
    journal.write_bytes(existing + b"".join(record(*row) for row in rows))


def apply(folder, journal=None, zone=ZONE, now=None):
    """Write the zone into the journal beside `folder` and note it in the
    done-marker, once. Returns True when it wrote something."""
    folder = Path(folder)
    marker = folder / win98_c.MARKER
    info = json.loads(marker.read_text()) if marker.is_file() else None
    if info is not None and info.get("time_zone") == zone:
        return False
    rows = values(folder, zone, now)
    append(journal or win98_c.journal(folder), rows)
    if info is not None:
        info["time_zone"] = zone
        marker.write_text(json.dumps(info, indent=2) + "\n")
    return True


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--folder", type=Path, default=win98_c.FOLDER)
    parser.add_argument("--zone", default=ZONE)
    parser.add_argument("--show", action="store_true", help="only print the values")
    args = parser.parse_args(argv)
    rows = values(args.folder, args.zone)
    if args.show:
        for name, kind, data in rows:
            print(name, kind, data.rstrip(b"\0").decode("cp1252") if kind == REG_SZ else data.hex())
        return 0
    win98_c.require(args.folder)
    if apply(args.folder, zone=args.zone):
        print(f"Time zone {args.zone!r} recorded in {win98_c.journal(args.folder)}")
    else:
        print(f"Time zone {args.zone!r} is already registered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
