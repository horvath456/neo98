#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Take back what one failed installation did to a Win98 folder and its registry.

Made for the Office XP setup of 19 October 2026: its System Files
Update copied the files of Internet Explorer 5.01, MDAC 2.5, Jet 4, the Windows
Installer and more over C:\\WINDOWS, failed half way and left an Explorer with
the browser files of two versions. Everything the user did before and after the
installation stays.

Two steps. `plan` compares a copy of the folder taken right after the failure
with the pristine folder of setup (build/win98-c.setup) and writes a manifest:

- the files the installation made (not in the pristine folder) to delete, each
  with the checksum it had after the failure;
- the files it replaced to take back from the pristine folder;
- the files of the pristine folder it removed to put back;
- the folders it made, removed when they end up empty;
- the lines it added to WIN.INI (named with `--ini-drop`: the user's own
  additions are not told apart from the installation's by a comparison);
- the byte range of the registry change journal (src/registry_persist.cpp)
  the installation wrote. The journal only grows, so the records of the
  sessions from the installation to the copy are one range; records written
  after the copy stay.

Which files the installation touched comes from a list of relative paths
(`--changed`, one per line, taken from the change times of the live folder:
a copy made with `cp` has fresh change times).

`apply` carries the manifest out on a folder and its journal. A file is only
deleted or replaced while it still has the checksum of the manifest, so a
file the user changed since stays; the journal range is only cut when the
journal still starts with the bytes of the copy. `--dry-run` reports only.

    python3 tools/install_rollback.py sessions JOURNAL
    python3 tools/install_rollback.py plan --after C --after-registry REG \\
        --pristine build/win98-c.setup --changed LIST --folders LIST --journal-from OFFSET \\
        [--keep PATH...] [--put-back PATH...] [--ini-drop PATH|SECTION|LINE...] \\
        --out MANIFEST
    python3 tools/install_rollback.py apply --drive C --registry REG \\
        --pristine build/win98-c.setup --manifest MANIFEST [--dry-run]

Run `apply` only while no NEO98 host has the folder open.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

SIGNATURE = b"NEO98REG1"
SIDECAR_PREFIX = ".neo98-"
# Files the user's own state lives in; a run rewrites them, and the pristine
# version would be older than what the user had.
VOLATILE = {"index.dat", "schedlog.txt", "sa.dat"}
SESSION_MARK = ("HKEY_USERS\\.Default\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer", "FaultKey")


def digest(path: Path) -> str | None:
    try:
        return hashlib.md5(path.read_bytes()).hexdigest()
    except OSError:
        return None


def find(root: Path, relative: str) -> Path | None:
    """`relative` under `root`, without regard to case, as the guest sees it."""
    current = root
    for part in relative.split("/"):
        try:
            names = os.listdir(current)
        except OSError:
            return None
        match = [name for name in names if name.lower() == part.lower()]
        if not match:
            return None
        current = current / match[0]
    return current


def records(journal: bytes):
    """(offset, kind, path, name) of every record, offset from the file start."""
    at, end = len(SIGNATURE), len(journal)

    def text(at):
        length = int.from_bytes(journal[at:at + 2], "little")
        return journal[at + 2:at + 2 + length].decode("cp1252"), at + 2 + length

    while at < end:
        start, kind = at, journal[at]
        path, at = text(at + 1)
        name = ""
        if kind >= 3:
            name, at = text(at)
        if kind == 3:
            at += 8 + int.from_bytes(journal[at + 4:at + 8], "little")
        if kind not in (1, 2, 3, 4) or at > end:
            return
        yield start, kind, path, name


def sessions(journal: bytes) -> list[int]:
    """Offsets of the records Explorer writes first in every session."""
    return [start for start, kind, path, name in records(journal)
            if kind == 3 and path.upper() == SESSION_MARK[0].upper() and name == SESSION_MARK[1]]


def listing(root: Path) -> dict[str, str]:
    out = {}
    for directory, folders, files in os.walk(root):
        for name in folders + files:
            if name.startswith(SIDECAR_PREFIX):
                continue
            relative = os.path.relpath(os.path.join(directory, name), root)
            out[relative.upper()] = relative
    return out


def ini_lines(path: Path) -> list[str]:
    return path.read_bytes().decode("cp1252").replace("\r\n", "\n").split("\n")


def plan(args) -> int:
    after, pristine = Path(args.after), Path(args.pristine)
    changed = [line.rstrip("\n") for line in Path(args.changed).read_text().splitlines() if line.strip()]
    keep = {path.lower() for path in args.keep}
    manifest = {"delete": [], "restore": [], "put_back": [], "folders": [], "ini": {}, "journal": None}
    for relative in sorted(changed):
        name = relative.rsplit("/", 1)[-1]
        if name.startswith(SIDECAR_PREFIX) or relative.lower() in keep:
            continue
        here, there = after / relative, find(pristine, relative)
        if not here.is_file():
            continue
        if there is None:
            manifest["delete"].append({"path": relative, "md5": digest(here)})
        elif name.lower() in VOLATILE or name.lower().endswith(".ini"):
            continue
        elif digest(there) != digest(here):
            manifest["restore"].append({"path": relative, "md5": digest(here), "pristine": digest(there)})
    # The folders the installation made (`--folders`, from the birth times of
    # the live folder): not in the pristine folder, and every file under them
    # is a file to delete.
    made = {line.strip().lower() for line in Path(args.folders).read_text().splitlines() if line.strip()}
    deleted = {entry["path"].lower() for entry in manifest["delete"]}
    pristine_names = listing(pristine)
    for directory, folders, files in os.walk(after, topdown=False):
        relative = os.path.relpath(directory, after)
        if relative.lower() not in made or relative.upper() in pristine_names or relative.lower() in keep:
            continue
        inside = [os.path.relpath(os.path.join(d, f), after).lower()
                  for d, _, fs in os.walk(directory) for f in fs if not f.startswith(SIDECAR_PREFIX)]
        if all(path in deleted for path in inside):
            manifest["folders"].append(relative)
    for entry in args.ini_drop:
        relative, section, line = entry.split("|", 2)
        manifest["ini"].setdefault(relative, []).append([section, line])
    for path in args.put_back:
        if find(pristine, path) is None:
            sys.exit(f"{path} is not in {pristine}")
        manifest["put_back"].append(path)
    journal = Path(args.after_registry).read_bytes()
    if not journal.startswith(SIGNATURE):
        sys.exit(f"{args.after_registry} is no NEO98 registry journal")
    starts = {start for start, *_ in records(journal)}
    if args.journal_from not in starts:
        sys.exit(f"{args.journal_from} is not the offset of a record; see `sessions`")
    manifest["journal"] = {"from": args.journal_from, "to": len(journal),
                           "md5": hashlib.md5(journal).hexdigest()}
    Path(args.out).write_text(json.dumps(manifest, indent=1) + "\n")
    print(f"{args.out}: {len(manifest['delete'])} to delete, {len(manifest['restore'])} to take back, "
          f"{len(manifest['put_back'])} to put back, {len(manifest['folders'])} folders, "
          f"journal bytes {args.journal_from}..{len(journal)}")
    return 0


def apply(args) -> int:
    drive, pristine = Path(args.drive), Path(args.pristine)
    manifest = json.loads(Path(args.manifest).read_text())
    dry = args.dry_run
    skipped, done = [], 0

    def note(text):
        print(("would " if dry else "") + text)

    journal_path = Path(args.registry)
    journal = journal_path.read_bytes()
    cut = manifest["journal"]
    if hashlib.md5(journal[:cut["to"]]).hexdigest() != cut["md5"]:
        sys.exit(f"{journal_path} does not start with the journal of the manifest; nothing changed")
    for entry in manifest["delete"]:
        target = find(drive, entry["path"])
        if target is None:
            continue
        if digest(target) != entry["md5"]:
            skipped.append(f"{entry['path']}: changed since the failure, left")
            continue
        note(f"delete {entry['path']}")
        if not dry:
            target.unlink()
        done += 1
    for entry in manifest["restore"]:
        target, source = find(drive, entry["path"]), find(pristine, entry["path"])
        current = digest(target) if target else None
        if current == entry["pristine"]:
            continue
        if current != entry["md5"] or source is None or digest(source) != entry["pristine"]:
            skipped.append(f"{entry['path']}: changed since the failure, left")
            continue
        note(f"take back {entry['path']}")
        if not dry:
            target.unlink()
            shutil.copy2(source, target)
        done += 1
    for relative in manifest["put_back"]:
        if find(drive, relative) is not None:
            continue
        source = find(pristine, relative)
        parent = find(drive, relative.rsplit("/", 1)[0]) if "/" in relative else drive
        note(f"put back {relative}")
        if not dry:
            if parent is None:
                parent = drive / relative.rsplit("/", 1)[0]
                parent.mkdir(parents=True, exist_ok=True)
            if source.is_dir():
                shutil.copytree(source, parent / source.name, copy_function=shutil.copy2)
            else:
                shutil.copy2(source, parent / source.name)
        done += 1
    for relative, lines in manifest["ini"].items():
        target = find(drive, relative)
        if target is None:
            continue
        wanted = {("[" + section.upper() + "]", line) for section, line in lines}
        out, section, removed = [], "", 0
        for line in ini_lines(target):
            if line.startswith("["):
                section = line.strip().upper()
            if (section, line) in wanted:
                removed += 1
                continue
            out.append(line)
        if removed:
            note(f"drop {removed} line(s) of {relative}")
            if not dry:
                stat = target.stat()
                target.write_bytes("\r\n".join(out).encode("cp1252"))
                os.utime(target, (stat.st_atime, stat.st_mtime))
            done += 1
    for relative in manifest["folders"]:
        target = find(drive, relative)
        if target is None:
            continue
        left = [name for name in os.listdir(target) if not name.startswith(SIDECAR_PREFIX)]
        if left:
            if not dry:
                skipped.append(f"{relative}/: still holds {len(left)} entries, left")
            continue
        note(f"remove folder {relative}")
        if not dry:
            shutil.rmtree(target)
        done += 1
    rest = journal[:cut["from"]] + journal[cut["to"]:]
    note(f"cut journal bytes {cut['from']}..{cut['to']} ({len(journal)} -> {len(rest)} bytes, "
         f"{len(journal) - cut['to']} bytes written since kept)")
    if not dry:
        temporary = journal_path.with_name(journal_path.name + ".rollback")
        temporary.write_bytes(rest)
        os.replace(temporary, journal_path)
    for line in skipped:
        print("skipped " + line)
    print(f"{done} changes{' planned' if dry else ''}, {len(skipped)} skipped")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    listing_ = commands.add_parser("sessions", help="offsets where the sessions of a journal begin")
    listing_.add_argument("journal")
    making = commands.add_parser("plan")
    making.add_argument("--after", required=True, help="copy of the folder right after the failure")
    making.add_argument("--after-registry", required=True, help="its registry journal")
    making.add_argument("--pristine", required=True)
    making.add_argument("--changed", required=True, help="relative paths the installation touched")
    making.add_argument("--folders", required=True, help="relative paths of the folders the installation made")
    making.add_argument("--journal-from", type=int, required=True, help="first journal record of the installation")
    making.add_argument("--keep", action="append", default=[], help="relative path that stays as it is")
    making.add_argument("--put-back", action="append", default=[], help="pristine path the installation removed")
    making.add_argument("--ini-drop", action="append", default=[],
                        help="PATH|SECTION|LINE the installation added to an INI file")
    making.add_argument("--out", required=True)
    doing = commands.add_parser("apply")
    doing.add_argument("--drive", required=True)
    doing.add_argument("--registry", required=True)
    doing.add_argument("--pristine", required=True)
    doing.add_argument("--manifest", required=True)
    doing.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "sessions":
        for offset in sessions(Path(args.journal).read_bytes()):
            print(offset)
        return 0
    return plan(args) if args.command == "plan" else apply(args)


if __name__ == "__main__":
    sys.exit(main())
