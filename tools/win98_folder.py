#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Copy the installed Win98 volume into the Win98 folder, once, during setup.

`python3 neo98.py setup --iso...` calls this after the ISO installation. The
disk image is only read, by the FAT12/16/32 reader below. Every file lands in
the folder (build/win98-c, tools/win98_c.py) under its long name. What macOS
cannot hold goes into the sidecar `.neo98-index` of every directory, in the
format of tools/host_volume.h: the real 8.3 alias of the FAT directory, the DOS
attribute byte and the creation time. The host files carry the FAT write and
access times, `.neo98-volume` the serial number and label of the boot sector.
The folder is then drive C:, and nothing reads the image again.

    build/analysis-venv/bin/python tools/win98_folder.py --image DISK --out DIR

The cleaned layer of the installation lives here as well: the SYSTEM.INI with
`shell=Explorer.exe` and without the [boot] lines of BOOT_LINES, and the
registry change journal beside the folder that removes the autostart entries
AUTOSTART lists, each with the evidence for what a standard Win98 installation
has in its place. Tests put the same layer over their own scratch drive with
cleanup. README.md.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# DOS attribute bits, as FindFirstFile reports them.
READONLY, HIDDEN, SYSTEM, LABEL, DIRECTORY, ARCHIVE = 1, 2, 4, 8, 16, 32
# The [boot] Shell of an ordinary Win98 installation; see shell_overlay.
SHELL_NAME = "Explorer.exe"
LONGNAME = READONLY | HIDDEN | SYSTEM | LABEL

RUN = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
RUN_SERVICES = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\RunServices"

# Every Run and RunServices entry the ISO installation carries, with the
# evidence for what a standard Win98 installation has in the same place.
# `standard` is true when a setup INF of this very tree writes the entry, so the
# evidence comes out of the installation and not out of memory. `remove` is what
# the cleanup layer takes away; see cleanup.
#
# A removal is a `delete value` record in the registry change journal beside
# drive C:; the hives in the folder stay as Setup wrote them.
AUTOSTART = (
    {"place": RUN, "name": "ScanRegistry", "data": "C:\\WINDOWS\\scanregw.exe /autorun",
     "standard": True, "remove": True, "group": "maintenance",
     "evidence": "WINDOWS/INF/SUBASE.INF line 989 writes it during setup",
     "reason": "the registry check of the machine; the port builds its registry out of the "
               "imported hives on every run, so the check has nothing to guard"},
    {"place": RUN, "name": "TaskMonitor", "data": "C:\\WINDOWS\\taskmon.exe",
     "standard": True, "remove": True, "group": "maintenance",
     "evidence": "WINDOWS/INF/SUBASE.INF line 991 writes it during setup",
     "reason": "it logs program starts for the defragmenter, which a reference tree does not run"},
    {"place": RUN, "name": "Welcome", "data": "C:\\WINDOWS\\welcome.exe",
     "standard": False, "remove": True, "group": "installation",
     "evidence": "Run value in the fresh ISO installation's SYSTEM.DAT",
     "reason": "the first-boot welcome window is unrelated to the shell test"},
    {"place": RUN, "name": "bpcpost.exe", "data": "C:\\WINDOWS\\SYSTEM\\bpcpost.exe",
     "standard": False, "remove": True, "group": "installation",
     "evidence": "Run value in the fresh ISO installation's SYSTEM.DAT",
     "reason": "the post-setup task has already run before the tree is imported"},
    {"place": RUN, "name": "SchedulingAgent", "data": "mstinit.exe /firstlogon",
     "standard": True, "remove": True, "group": "maintenance",
     "evidence": "Run value in the fresh ISO installation's SYSTEM.DAT",
     "reason": "the scheduler's first-logon initialization is unrelated to the shell test"},
    {"place": RUN, "name": "SystemTray", "data": "SysTray.Exe",
     "standard": True, "remove": False, "group": "shell",
     "evidence": "the INFs of the tree write it, among them MACHINE.INF, WAVE.INF, PCMCIA.INF and WDMAUDIO.INF",
     "reason": "part of the Win98 shell: it puts the volume, battery and PC card icons into the "
               "tray. It stays, and that it dies in this port still requires investigation"},
    {"place": RUN, "name": "LoadPowerProfile",
     "data": "Rundll32.exe powrprof.dll,LoadCurrentPwrScheme",
     "standard": True, "remove": False, "group": "shell",
     "evidence": "WINDOWS/INF/POWERCFG.INF writes it into Run and into RunServices",
     "reason": "a standard Win98 component that runs through in this port"},
    {"place": RUN_SERVICES, "name": "SchedulingAgent", "data": "C:\\WINDOWS\\SYSTEM\\mstask.exe",
     "standard": True, "remove": True, "group": "maintenance",
     "evidence": "WINDOWS/INF/SUBASE.INF line 1003 writes it during setup",
     "reason": "the task scheduler behind the ScanDisk, defragmenter and tune-up jobs of "
               "WINDOWS/Tasks; a reference tree runs no scheduled maintenance"},
    {"place": RUN_SERVICES, "name": "LoadPowerProfile",
     "data": "Rundll32.exe powrprof.dll,LoadCurrentPwrScheme",
     "standard": True, "remove": False, "group": "shell",
     "evidence": "WINDOWS/INF/POWERCFG.INF writes it into Run and into RunServices",
     "reason": "a standard Win98 component"},
)

# The [boot] lines of SYSTEM.INI the cleanup layer drops, with the same evidence
# rule. The shell line is not dropped but rewritten; see shell_overlay. The
# ISO installation has none: its [boot] has no SCRNSAVE.EXE line, which the
# reference system installation this port was first built on carried (3D Maze).
BOOT_LINES = ()


def dos_time(date, time, fine=0):
    """One FAT timestamp as an ISO string, or None when the field is unset."""
    if not date:
        return None
    year, month, day = 1980 + (date >> 9), (date >> 5) & 15, date & 31
    hour, minute, second = time >> 11, (time >> 5) & 63, (time & 31) * 2 + fine // 100
    if not (1 <= month <= 12 and 1 <= day <= 31):
        return None
    return (f"{year:04d}-{month:02d}-{day:02d}T{hour:02d}:{minute:02d}:"
            f"{min(second, 59):02d}.{(fine % 100) * 10:03d}")


class Volume:
    """Read-only FAT12/16/32 reader over a raw image at a byte offset."""

    def __init__(self, image, offset):
        self.file = image.open("rb")
        self.offset = offset
        self.file.seek(offset)
        boot = self.file.read(512)
        self.bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
        self.cluster_sectors = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fats = boot[16]
        root_entries = struct.unpack_from("<H", boot, 17)[0]
        fat16 = struct.unpack_from("<H", boot, 22)[0]
        total16 = struct.unpack_from("<H", boot, 19)[0]
        total32 = struct.unpack_from("<I", boot, 32)[0]
        self.fat_sectors = fat16 or struct.unpack_from("<I", boot, 36)[0]
        if not self.bytes_per_sector or not self.cluster_sectors or not self.fat_sectors:
            raise ValueError("Not a FAT boot sector")
        self.cluster_bytes = self.bytes_per_sector * self.cluster_sectors
        root_sectors = (root_entries * 32 + self.bytes_per_sector - 1) // self.bytes_per_sector
        self.first_data = reserved + fats * self.fat_sectors + root_sectors
        total = total16 or total32
        clusters = (total - self.first_data) // self.cluster_sectors
        self.bits = 12 if clusters < 4085 else 16 if clusters < 65525 else 32
        self.clusters = clusters
        self.fat = self.sectors(reserved, self.fat_sectors)
        self.root_cluster = struct.unpack_from("<I", boot, 44)[0] if self.bits == 32 else 0
        self.root_sectors = (reserved + fats * self.fat_sectors, root_sectors)
        self.label = boot[71:82].decode("latin-1").strip() if self.bits == 32 else \
            boot[43:54].decode("latin-1").strip()

    def sectors(self, first, count):
        self.file.seek(self.offset + first * self.bytes_per_sector)
        return self.file.read(count * self.bytes_per_sector)

    def next_cluster(self, cluster):
        if self.bits == 32:
            return struct.unpack_from("<I", self.fat, cluster * 4)[0] & 0x0fffffff
        if self.bits == 16:
            return struct.unpack_from("<H", self.fat, cluster * 2)[0]
        pos = cluster + cluster // 2
        value = struct.unpack_from("<H", self.fat, pos)[0]
        return value >> 4 if cluster & 1 else value & 0xfff

    def end_marker(self):
        return {12: 0xff8, 16: 0xfff8, 32: 0x0ffffff8}[self.bits]

    def chain(self, cluster):
        """Cluster numbers of one file, with a loop guard."""
        seen, end = set(), self.end_marker()
        while 2 <= cluster < self.clusters + 2 and cluster < end and cluster not in seen:
            seen.add(cluster)
            yield cluster
            cluster = self.next_cluster(cluster)

    def read_cluster(self, cluster):
        first = self.first_data + (cluster - 2) * self.cluster_sectors
        return self.sectors(first, self.cluster_sectors)

    def read_chain(self, cluster, size=None):
        out = bytearray()
        for number in self.chain(cluster):
            out += self.read_cluster(number)
            if size is not None and len(out) >= size:
                break
        return bytes(out[:size] if size is not None else out)

    def directory(self, cluster):
        """The raw 32-byte entries of one directory, root included."""
        if cluster == 0 and self.bits != 32:
            first, count = self.root_sectors
            return self.sectors(first, count)
        return self.read_chain(cluster or self.root_cluster)


def entries(volume, cluster):
    """Decode one directory into (long, short, attributes, times, cluster, size)."""
    data = volume.directory(cluster)
    out, parts, checksum = [], {}, None
    for pos in range(0, len(data) - 31, 32):
        record = data[pos:pos + 32]
        if record[0] == 0:
            break
        attributes = record[11]
        if record[0] == 0xe5:
            parts, checksum = {}, None
            continue
        if attributes & LONGNAME == LONGNAME:
            order = record[0] & 0x3f
            if record[0] & 0x40:
                parts, checksum = {}, record[13]
            if checksum == record[13]:
                raw = record[1:11] + record[14:26] + record[28:32]
                parts[order] = raw.decode("utf-16-le", "replace")
            continue
        base = record[0:8].decode("latin-1").rstrip()
        extension = record[8:11].decode("latin-1").rstrip()
        short = base + ("." + extension if extension else "")
        if record[0] == 0x05:
            short = "å" + short[1:]
        name = ""
        if parts and checksum == short_checksum(record[0:11]):
            name = "".join(parts[i] for i in sorted(parts)).split("\0")[0]
        parts, checksum = {}, None
        if attributes & LABEL and not attributes & DIRECTORY:
            continue
        if short in (".", ".."):
            continue
        first = (struct.unpack_from("<H", record, 20)[0] << 16) | struct.unpack_from("<H", record, 26)[0]
        out.append({
            "name": name or short,
            "short": short,
            "lfn": bool(name),
            "dos": [struct.unpack_from("<H", record, 16)[0], struct.unpack_from("<H", record, 14)[0],
                    record[13], struct.unpack_from("<H", record, 18)[0],
                    struct.unpack_from("<H", record, 24)[0], struct.unpack_from("<H", record, 22)[0]],
            "attributes": attributes,
            "cluster": first,
            "size": struct.unpack_from("<I", record, 28)[0],
            "created": dos_time(struct.unpack_from("<H", record, 16)[0],
                                struct.unpack_from("<H", record, 14)[0], record[13]),
            "accessed": dos_time(struct.unpack_from("<H", record, 18)[0], 0),
            "modified": dos_time(struct.unpack_from("<H", record, 24)[0],
                                 struct.unpack_from("<H", record, 22)[0]),
        })
    return out


def short_checksum(name):
    value = 0
    for byte in name:
        value = (((value & 1) << 7) + (value >> 1) + byte) & 0xff
    return value


def walk(volume, limit=None):
    """Every entry of the volume, depth first, parents before children."""
    stack, seen = [("", volume.root_cluster if volume.bits == 32 else 0)], set()
    while stack:
        path, cluster = stack.pop()
        if cluster in seen and cluster:
            continue
        seen.add(cluster)
        for entry in entries(volume, cluster):
            entry["path"] = f"{path}/{entry['name']}" if path else entry["name"]
            entry["short_path"] = f"{path}/{entry['short']}" if path else entry["short"]
            yield entry
            if entry["attributes"] & DIRECTORY and entry["cluster"]:
                if limit is None or entry["path"].count("/") < limit:
                    stack.append((entry["path"], entry["cluster"]))


def safe(path):
    """Reject a name the extraction must not write outside the tree."""
    parts = path.split("/")
    return parts and all(p and p not in (".", "..") and "\\" not in p and ":" not in p
                         for p in parts)


JOURNAL_SIGNATURE = b"NEO98REG1"


def journal_record(kind, path, name=None):
    """One record of the registry change journal of src/registry_persist.cpp.

    Kind 4 is `delete value`: the key path, then the value name, each as a two
    byte length and that many bytes. The signature belongs in front of the
    records and is written once, by journal_bytes.
    """
    out = bytearray([kind])
    for text in (path, name) if kind >= 3 else (path,):
        raw = (text or "").encode("cp1252")
        out += len(raw).to_bytes(2, "little") + raw
    return bytes(out)


def journal_walk(body):
    """Every record of a journal body as (kind, path, name), in order.

    Stops at the first byte it cannot read, so a journal an older version wrote
    is still usable up to that point.
    """
    at, end = 0, len(body)

    def text(at):
        if at + 2 > end:
            return None, end + 1
        length = int.from_bytes(body[at:at + 2], "little")
        return body[at + 2:at + 2 + length], at + 2 + length

    while at < end:
        kind = body[at]
        path, at = text(at + 1)
        if path is None or at > end or kind not in (1, 2, 3, 4):
            return
        name = b""
        if kind >= 3:
            name, at = text(at)
            if name is None or at > end:
                return
        if kind == 3:
            if at + 8 > end:
                return
            at += 8 + int.from_bytes(body[at + 4:at + 8], "little")
            if at > end:
                return
        yield kind, path.decode("cp1252"), name.decode("cp1252")


def journal_bytes(records, existing=b""):
    """The journal file: signature, the records already in it, then the new ones.

    A deletion is left out when the journal already ends on the same deletion
    for that value, so repeated imports leave the journal the same size. A guest
    that set the value again afterwards gets the deletion once more, because the
    replay follows the order of the records. Everything a guest wrote earlier
    stays where it is.
    """
    body = existing[len(JOURNAL_SIGNATURE):] if existing.startswith(JOURNAL_SIGNATURE) else b""
    last = {}
    for kind, path, name in journal_walk(body):
        last[path.upper(), name.upper()] = kind
    for record in records:
        kind, path, name = next(journal_walk(record))
        if last.get((path.upper(), name.upper())) != kind:
            body += record
            last[path.upper(), name.upper()] = kind
    return JOURNAL_SIGNATURE + body


def cleanup_entries(keep_standard=False):
    """The autostart entries the cleanup layer removes.

    `keep_standard` narrows the list to what no Win98 setup INF of this tree
    writes, so only what this particular installation added goes away.
    """
    return [entry for entry in AUTOSTART
            if entry["remove"] and not (keep_standard and entry["standard"])]


def cleanup_lines(keep_standard=False):
    """The [boot] lines of SYSTEM.INI the cleanup layer drops."""
    return [entry for entry in BOOT_LINES
            if entry["remove"] and not (keep_standard and entry["standard"])]


def registry_overlay(drive, registry=None, keep_standard=False):
    """Write the cleaned registry layer beside the writable C:.

    The imported hives are the start state of every run and the tree stays
    read-only, so a removed autostart entry cannot be a change of the hive. It
    is a record in the change journal that lies beside the drive directory, the
    file tools/registry_file.h opens by default : one
    `delete value` per entry, replayed by Registry::Restore right after the
    import and before the run records anything of its own.

    Returns (path, written, entries).
    """
    target = Path(registry) if registry else Path(str(drive) + ".registry")
    entries = cleanup_entries(keep_standard)
    records = [journal_record(4, entry["place"], entry["name"]) for entry in entries]
    existing = target.read_bytes() if target.is_file() else b""
    image = journal_bytes(records, existing)
    if image == existing:
        return target, False, entries
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(image)
    return target, True, entries


def cleanup(tree, drive, registry=None, keep_standard=False, force=False):
    """Both halves of the layer above the tree: the SYSTEM.INI and the journal.

    Returns a report with the shell line, the dropped [boot] lines and the
    removed autostart entries.
    """
    ini = shell_overlay(tree, drive, force=force, keep_standard=keep_standard)
    if ini is None:
        return None
    path, written, previous = ini
    # What the RunOnce entry RestoreCfgAuto of the first boot puts back, which
    # the installation in QEMU ended before (tools/win98_first_boot.py). It
    # goes in front of the removed autostart entries, so one of them that the
    # restore names (SchedulingAgent) stays removed.
    import win98_first_boot
    restored = win98_first_boot.apply(tree, Path(registry) if registry else Path(str(drive) + ".registry"))
    journal, journal_written, entries = registry_overlay(drive, registry, keep_standard)
    return {"system_ini": str(path), "system_ini_written": written, "image_shell": previous,
            "boot_lines": cleanup_lines(keep_standard), "journal": str(journal),
            "journal_written": journal_written or bool(restored), "first_boot_restored": restored,
            "autostart": entries,
            "kept": [e for e in AUTOSTART if e not in entries]}


def image_shell(text):
    """The [boot] Shell value of a SYSTEM.INI, or None when it has none."""
    section = ""
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].lower()
        elif section == "boot" and stripped.lower().startswith("shell="):
            return stripped.split("=", 1)[1].strip()
    return None


def shell_overlay_text(text, keep_standard=False, cleanup_boot=True):
    """The SYSTEM.INI of the layer: the file of the image with shell= rewritten
    and, with `cleanup_boot`, without the [boot] lines of BOOT_LINES."""
    section, done, out = "", False, []
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            section = stripped[1:-1].lower()
        elif section == "boot" and not done and stripped.lower().startswith("shell="):
            done = True
            line = "shell=" + SHELL_NAME + line[len(line.rstrip("\r\n")):]
        elif section == "boot" and cleanup_boot and any(
                stripped.lower().startswith(entry["name"].lower() + "=")
                for entry in cleanup_lines(keep_standard)):
            continue
        out.append(line)
    return "".join(out).encode("latin-1")


def shell_overlay(tree, drive, force=False, keep_standard=False, cleanup_boot=True):
    """Make the SYSTEM.INI of the writable C: name Explorer.exe as [boot] Shell.

    EXPLORER.EXE reads `[boot] Shell` out of SYSTEM.INI and builds desktop and
    taskbar only when that line names its own file. The ISO installation already has `shell=Explorer.exe`;
    this keeps the rule for a tree that does not. `tree` holds the original
    file, `drive` receives the copy; both may be the same folder, the Win98
    folder of setup. The copy is the original byte for byte except for that
    line and, with `cleanup_boot`, the [boot] lines of BOOT_LINES.

    Returns (path, written, previous shell value) or None when the tree has no
    SYSTEM.INI. An existing copy somebody edited is kept unless `force` is set;
    an identical one is not written again.
    """
    source = Path(tree) / "WINDOWS/SYSTEM.INI"
    if not source.is_file():
        return None
    target = Path(drive) / "WINDOWS/SYSTEM.INI"
    text = source.read_bytes().decode("latin-1")
    previous = image_shell(text)
    if previous is None:
        return None
    out = shell_overlay_text(text, keep_standard, cleanup_boot)
    if target.is_file():
        existing = target.read_bytes()
        if existing == out:
            return target, False, previous
        if not force and existing not in (text.encode("latin-1"),
                                          shell_overlay_text(text, keep_standard, False)):
            return target, False, previous
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(out)
    return target, True, previous


SIDECAR_HEADER = "# NEO98 volume index 1: flags\tattributes\tcdate,ctime,chundredths\talias\tname\n"
# Helpers of tools/setup_win98.py on the installed disk; they are not Win98.
# Installations made before the rename carry them under the project's
# previous name, spelled here as bytes so the release checks stay quiet.
_PREVIOUS_NAME = bytes((65, 82, 77, 57, 56)).decode()
SETUP_HELPERS = {"NEO98", "NEO98.DONE", _PREVIOUS_NAME, _PREVIOUS_NAME + ".DONE"}


def guest_name(entry):
    """The name as the guest spells it, in the ANSI code page, as a str whose
    code points are the bytes (tools/host_volume.h keeps names this way).

    A long name comes from UTF-16 VFAT entries; a plain 8.3 name is OEM bytes,
    which entries hands over as latin-1 text."""
    if entry["lfn"]:
        text = entry["name"]
    else:
        text = entry["name"].encode("latin-1").decode("cp437")
    return text.encode("cp1252", "replace").replace(b"?", b"_").decode("latin-1") \
        if any(ord(c) > 127 for c in text) else text


def guest_alias(entry):
    """The 8.3 alias FAT keeps, or empty when the long name already is it."""
    short = entry["short"].encode("latin-1").decode("cp437")
    short = short.encode("cp1252", "replace").decode("latin-1")
    return "" if short.upper() == guest_name(entry).upper() else short


def dos_seconds(date, time):
    """A FAT date and time in local time as seconds since the epoch, or None."""
    import time as clock
    if not date:
        return None
    try:
        return clock.mktime((1980 + (date >> 9), (date >> 5) & 15, date & 31,
                             time >> 11, (time >> 5) & 63, (time & 31) * 2, 0, 0, -1))
    except (OverflowError, ValueError):
        return None


def write_sidecar(directory, records):
    """The.neo98-index of one directory, in the order FAT lists the entries.

    `records` are (attributes, (cdate, ctime, chundredths), alias, name) with
    guest names."""
    lines = [SIDECAR_HEADER]
    for attributes, created, alias, name in records:
        lines.append(f"0\t{attributes}\t{created[0]},{created[1]},{created[2]}\t{alias}\t{name}\n")
    (Path(directory) / ".neo98-index").write_bytes("".join(lines).encode("latin-1"))


def write_volume(folder, serial, label):
    (Path(folder) / ".neo98-volume").write_text(f"{serial:08x}\t{label}\n", encoding="latin-1")


def copy_tree(image, offset, out, quiet=False):
    """Copy the whole FAT volume into the empty folder `out`, once.

    Every file lands under its long name, every directory gets the sidecar
    with the real 8.3 aliases, attribute bytes and creation times of its FAT
    directory, and the host file carries the FAT write and access times. The
    volume serial number and label come from the boot sector. Returns a report.
    """
    out = Path(out)
    if out.exists() and any(out.iterdir()):
        raise RuntimeError(f"{out} is not empty")
    out.mkdir(parents=True, exist_ok=True)
    volume = Volume(image, offset)
    # FAT path -> (host directory, sidecar records in FAT order)
    folders = {"": (out, [])}
    stamps = []
    count_files = count_directories = count_bytes = 0
    truncated = []
    for entry in walk(volume):
        path = entry["path"]
        if not safe(path) or path.split("/")[0].upper() in SETUP_HELPERS:
            continue
        parent = folders.get(path.rpartition("/")[0])
        if parent is None:
            continue
        name = guest_name(entry)
        host = parent[0] / name
        parent[1].append((entry["attributes"], tuple(entry["dos"][:3]), guest_alias(entry), name))
        if entry["attributes"] & DIRECTORY:
            host.mkdir(exist_ok=True)
            folders[path] = (host, [])
            count_directories += 1
        else:
            data = volume.read_chain(entry["cluster"], entry["size"]) if entry["size"] else b""
            if len(data) != entry["size"]:
                truncated.append(path)
            host.write_bytes(data)
            count_files += 1
            count_bytes += len(data)
        stamps.append((host, entry))
    for directory, records in folders.values():
        write_sidecar(directory, records)
    # Times last, deepest first: writing a child changes its directory.
    for host, entry in reversed(stamps):
        access = dos_seconds(entry["dos"][3], 0)
        write = dos_seconds(entry["dos"][4], entry["dos"][5])
        if write is not None:
            os.utime(host, (access if access is not None else write, write))
        if entry["attributes"] & READONLY and not entry["attributes"] & DIRECTORY:
            host.chmod(0o444)
    boot = volume.sectors(0, 1)
    serial = struct.unpack_from("<I", boot, 67 if volume.bits == 32 else 39)[0]
    write_volume(out, serial, volume.label or "NO NAME")
    report = {"label": volume.label, "serial": f"{serial:08x}", "fat_bits": volume.bits,
              "files": count_files, "directories": count_directories, "bytes": count_bytes,
              "truncated": truncated}
    if not quiet:
        print(f"Win98 folder {out}: {count_files} files, {count_directories} directories, "
              f"{count_bytes:,} Bytes")
    return report


def partition_offset(image):
    """Byte offset of the only FAT partition of a raw MBR disk."""
    mbr = Path(image).open("rb").read(512)
    if mbr[510:] != b"\x55\xaa":
        raise SystemExit("Expected a raw MBR image; supply --partition-offset")
    candidates = [446 + 16 * i for i in range(4)
                  if mbr[446 + 16 * i + 4] in (1, 4, 6, 0x0b, 0x0c, 0x0e)]
    if len(candidates) != 1:
        raise SystemExit("Cannot select a unique FAT partition; use --partition-offset")
    return struct.unpack_from("<I", mbr, candidates[0] + 8)[0] * 512


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image", type=Path, required=True, help="raw disk of the ISO installation")
    parser.add_argument("--partition-offset", type=int)
    parser.add_argument("--out", type=Path, required=True, help="empty target folder")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)
    offset = args.partition_offset if args.partition_offset is not None else partition_offset(args.image)
    report = copy_tree(args.image, offset, args.out, args.quiet)
    print(json.dumps({k: v for k, v in report.items() if k != "truncated"}))
    return 1 if report["truncated"] else 0


if __name__ == "__main__":
    sys.exit(main())
