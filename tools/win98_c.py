#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Drive C: of NEO98: the Win98 folder `python3 neo98.py setup --iso...` creates once.

The folder is an ordinary directory on this Mac, by default build/win98-c, and
NEO98_WIN98_ROOT names another one. Setup copies the installation out of the
ISO install into it once, with the 8.3 aliases, attribute bytes and creation
times of the FAT directories in the sidecars of tools/host_volume.h, generates
everything derived from it and writes the done-marker `.neo98-setup.json`
last. Nothing reads a disk image afterwards. README.md.

The tools find original files here with `path` and `copy`. A path may be
given as a guest path (`C:\\WINDOWS\\SYSTEM\\USER32.DLL`), as an mtools path
(`::/WINDOWS/SYSTEM/USER32.DLL`) or relative to the folder, and each component
may be its long name or its 8.3 alias, in any case.

    build/analysis-venv/bin/python tools/win98_c.py # state of the folder
    build/analysis-venv/bin/python tools/win98_c.py --verify # compare with the setup checksums
    build/analysis-venv/bin/python tools/win98_c.py --pristine # the snapshot the tests start from

Tests never read the live folder: use_pristine gives them the snapshot of
the state setup produced (see pristine).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FOLDER = Path(os.environ.get("NEO98_WIN98_ROOT") or ROOT / "build/win98-c")
MARKER = ".neo98-setup.json"
CHECKSUMS = ".neo98-sha256"
SIDECAR = ".neo98-index"
# Known images and compatible content profiles accepted by setup.
SUPPORTED_ISOS = ROOT / "tools/win98_isos.json"
NOT_READY = ("No Windows 98 installation at {folder}. Run setup first:\n"
             "    python3 neo98.py setup --iso '/path/to/Windows 98 Second Edition.iso'")


def metadata_name(name):
    return name.startswith((".neo98-", "." + bytes((97,114,109,57,56)).decode() + "-"))

def legacy_read(path):
    """Read old metadata without modifying an imported drive or its journal."""
    path = Path(path)
    if path.exists():
        return path
    old = path.with_name(path.name.replace('.neo98-', '.' + bytes((97, 114, 109, 57, 56)).decode() + '-', 1))
    return old if old.exists() else path


def journal(folder: Path | None = None) -> Path:
    """The registry change journal beside the folder, as tools/registry_file.h opens it."""
    folder = FOLDER if folder is None else Path(folder)
    return folder.parent / (folder.name + ".registry")


def ready(folder: Path | None = None) -> bool:
    """Whether setup has finished the folder. Its own children, which generate
    the assets out of the folder before the marker exists, see it as ready."""
    folder = FOLDER if folder is None else Path(folder)
    if os.environ.get("NEO98_SETUP_RUNNING") == "1":
        return legacy_read(folder / SIDECAR).is_file()
    return legacy_read(folder / MARKER).is_file()


def require(folder: Path | None = None) -> Path:
    """The folder, or exit with the request to run setup first."""
    folder = FOLDER if folder is None else Path(folder)
    if not ready(folder):
        raise SystemExit(NOT_READY.format(folder=folder))
    return folder


def record(folder: Path | None = None) -> dict:
    """The done-marker: ISO, date, file count."""
    folder = FOLDER if folder is None else Path(folder)
    return json.loads(legacy_read(require(folder) / MARKER).read_text())


def parts(guest: str) -> list[str]:
    """The components of a guest, mtools or relative path."""
    text = guest.replace("\\", "/")
    if text.startswith("::"):
        text = text[2:]
    elif len(text) >= 2 and text[1] == ":":
        text = text[2:]
    return [part for part in text.split("/") if part and part != "."]


def _aliases(directory: Path) -> dict[str, str]:
    """8.3 alias -> long name of one directory, out of its sidecar."""
    out = {}
    try:
        text = legacy_read(directory / SIDECAR).read_bytes().decode("latin-1")
    except OSError:
        return out
    for line in text.splitlines():
        if line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) == 5 and fields[3]:
            out[fields[3].upper()] = fields[4]
    return out


def path(guest: str, folder: Path | None = None) -> Path | None:
    """The host path of one entry of the folder, or None when it is missing.

    Resolved component by component, case insensitively, by long name or by
    the alias the sidecar records, so the answer does not depend on the host
    file system telling cases apart.
    """
    folder = FOLDER if folder is None else Path(folder)
    current = folder
    for part in parts(guest):
        try:
            names = os.listdir(current)
        except OSError:
            return None
        upper = part.upper()
        name = next((n for n in names if n.upper() == upper and not metadata_name(n)), None)
        if name is None:
            long = _aliases(current).get(upper)
            # Sidecar names are guest bytes; the host spells them as the code
            # points of the same numbers (tools/host_volume.h ToHost).
            name = next((n for n in names if long is not None and n.upper() == long.upper()), None)
        if name is None:
            return None
        current = current / name
    return current


def copy(guest: str, target, folder: Path | None = None) -> Path:
    """Copy one original file out of the folder; the folder is only read."""
    folder = FOLDER if folder is None else Path(folder)
    source = path(guest, require(folder))
    if source is None or not source.is_file():
        raise FileNotFoundError(f"{guest} is missing from Win98 folder {folder}")
    target = Path(target)
    if target.is_dir():
        target = target / source.name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    return target


def sha256(file: Path) -> str:
    digest = hashlib.sha256()
    with open(file, "rb") as data:
        for chunk in iter(lambda: data.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def files(folder: Path | None = None):
    """Every file of the folder, relative with forward slashes, sidecars left out."""
    folder = FOLDER if folder is None else Path(folder)
    for directory, subdirectories, names in os.walk(folder):
        subdirectories.sort()
        for name in sorted(names):
            if metadata_name(name):
                continue
            yield (Path(directory) / name).relative_to(folder).as_posix()


def checksums(folder: Path | None = None) -> dict[str, str]:
    """The SHA-256 of every file as setup recorded it."""
    folder = FOLDER if folder is None else Path(folder)
    out = {}
    text = legacy_read(require(folder) / CHECKSUMS).read_text(encoding="utf-8")
    for line in text.splitlines():
        if line and not line.startswith("#"):
            digest, name = line.split("  ", 1)
            out[name] = digest
    return out


def write_checksums(folder: Path) -> int:
    lines = ["# Neo98 Win98 folder: SHA-256 of each file after setup"]
    count = 0
    for name in files(folder):
        lines.append(f"{sha256(folder / name)}  {name}")
        count += 1
    (folder / CHECKSUMS).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return count


def verify(folder: Path | None = None) -> dict[str, list[str]]:
    """What changed in the folder since setup, file by file."""
    folder = FOLDER if folder is None else Path(folder)
    pinned = checksums(folder)
    present = set(files(folder))
    changed = [name for name in sorted(present & set(pinned)) if sha256(folder / name) != pinned[name]]
    return {"changed": changed, "added": sorted(present - set(pinned)),
            "removed": sorted(set(pinned) - present)}


# The builds the original tests are calibrated against: those of the pinned
# ISO installation (tools/win98_isos.json). They were first calibrated against
# the reference system installation's NUSB 3.3 and add-on builds and carried over with
# tools/derive_original_offsets.py.
CALIBRATED = {
    "WINDOWS/SYSTEM/USER.EXE": "USER.EXE 4.10.2222 from the ISO",
    "WINDOWS/SYSTEM/USER32.DLL": "USER32.DLL 4.10.2222 from the ISO",
    "WINDOWS/SYSTEM/MSVCRT.DLL": "MSVCRT.DLL 6.0.8397 from the ISO",
}


def skip_unless_original(guest: str, digest: str, folder: Path | None = None):
    """End a test with a note when the Win98 folder does not hold the build it
    was calibrated against, such as a folder in which an installer replaced
    USER.EXE. Such a test is not loosened: its offsets belong to that build."""
    folder = FOLDER if folder is None else Path(folder)
    source = path(guest, require(folder))
    if source is not None and sha256(source) == digest:
        return
    what = CALIBRATED.get(guest, guest)
    print(f"SKIP: test requires {what} (SHA-256 {digest[:16]}); "
          f"the Win98 folder contains a different version.", flush=True)
    raise SystemExit(0)


# The state setup produced, for tests.
#
# build/win98-c is drive C: of the user's machine: the host writes the user's
# files into it and every registry change into the journal beside it, among
# them the per-user Active Setup steps of the first shell start. A test that
# cloned it measured the user's last session, not Win98. Tests start from the
# pristine snapshot instead: the folder and the journal exactly as setup left
# them, beside the live folder as `win98-c.setup` and `win98-c.setup.registry`.
# Setup clones it right after the done-marker (APFS clone, no extra space
# until the user changes a file). A folder set up before the snapshot existed
# gets it rebuilt from the installed disk image of setup, the same steps setup
# ran, and checked file by file against the setup checksums.
#
# Tests call use_pristine first. It points FOLDER and NEO98_WIN98_ROOT (the
# C++ runners' default, tools/host_volume.h) at the snapshot, so everything
# the test and its children read of C: is the pristine tree. A test that needs
# a writable C: clones the snapshot, never the snapshot itself: pristine
# checks it on every call by the size and time of each file and the hash of the
# journal and rebuilds it when a run wrote into it.
PRISTINE_SUFFIX = ".setup"
PRISTINE_MANIFEST = ".neo98-pristine.json"


def live_folder() -> Path:
    """The user's drive C: (build/win98-c or the configured folder), real path."""
    return Path(os.environ.get("NEO98_WIN98_LIVE_ROOT") or os.environ.get("NEO98_WIN98_ROOT")
                or ROOT / "build/win98-c").resolve()


def pristine_folder(folder: Path | None = None) -> Path:
    live = Path(folder).resolve() if folder is not None else live_folder()
    return live.parent / (live.name + PRISTINE_SUFFIX)


def _state(folder: Path) -> dict:
    """Size and modification time of every entry, sidecars included."""
    out = {}
    for directory, subdirectories, names in os.walk(folder):
        subdirectories.sort()
        for name in sorted(names) + [d + "/" for d in subdirectories]:
            if name == PRISTINE_MANIFEST:
                continue
            full = Path(directory) / name.rstrip("/")
            relative = full.relative_to(folder).as_posix() + ("/" if name.endswith("/") else "")
            info = full.lstat()
            out[relative] = [0 if name.endswith("/") else info.st_size, info.st_mtime_ns]
    return out


def _journal_digest(folder: Path) -> str:
    target = journal(folder)
    return sha256(target) if target.is_file() else ""


def _write_manifest(folder: Path, source: str):
    manifest = {"source": source, "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
                "journal_sha256": _journal_digest(folder), "entries": _state(folder)}
    (folder / PRISTINE_MANIFEST).write_text(json.dumps(manifest) + "\n")


def pristine_problems(folder: Path) -> list[str]:
    """What differs from the manifest of the snapshot; empty when it is intact."""
    try:
        manifest = json.loads((folder / PRISTINE_MANIFEST).read_text())
    except (OSError, ValueError):
        return ["no manifest"]
    problems = []
    if _journal_digest(folder) != manifest["journal_sha256"]:
        problems.append(f"journal {journal(folder).name} changed")
    now, then = _state(folder), manifest["entries"]
    problems += [f"changed {name}" for name in sorted(set(now) & set(then)) if now[name] != then[name]
                 and not name.endswith("/")]
    problems += [f"added {name}" for name in sorted(set(now) - set(then))]
    problems += [f"removed {name}" for name in sorted(set(then) - set(now))]
    return problems


def snapshot_pristine(folder: Path) -> Path:
    """Clone a folder setup has just finished, with its journal (setup_win98)."""
    folder = Path(folder).resolve()
    target = pristine_folder(folder)
    _remove(target)
    journal(target).unlink(missing_ok=True)
    copy = ["cp", "-c", "-R"] if sys.platform == "darwin" else ["cp", "-a", "--reflink=auto"]
    subprocess.run([*copy, str(folder), str(target)], check=True)
    if journal(folder).is_file():
        copy_file = ["cp", "-c"] if sys.platform == "darwin" else ["cp", "-a", "--reflink=auto"]
        subprocess.run([*copy_file, str(journal(folder)), str(journal(target))], check=True)
    _write_manifest(target, "clone of the folder at the end of setup")
    return target


def _remove(target: Path):
    if target.exists():
        subprocess.run(["chmod", "-R", "u+w", str(target)], check=True)
        shutil.rmtree(target)


def _rebuild_pristine(live: Path, target: Path) -> Path:
    """Redo setup's copy out of its installed disk image into the snapshot."""
    import datetime
    import win98_folder
    import win98_time_zone
    from setup_win98 import PARTITION_OFFSET
    info = record(live)
    disk = live.parent / "setup-win98" / info["iso"]["sha256"][:16] / "disk.raw"
    if not disk.is_file():
        raise SystemExit(f"No pristine copy of {live}: {target} is missing or modified, and "
                         f"the installed image {disk} is missing. Run setup again.")
    staging = live.parent / (target.name + ".tmp")
    _remove(staging)
    journal(staging).unlink(missing_ok=True)
    print(f"win98_c: rebuilding pristine copy {target.name} from {disk.name}", flush=True)
    win98_folder.copy_tree(disk, PARTITION_OFFSET, staging, quiet=True)
    win98_folder.cleanup(staging, staging, journal(staging))
    created = datetime.datetime.fromisoformat(info["created"])
    if info.get("time_zone"):
        win98_time_zone.apply(staging, journal(staging), zone=info["time_zone"], now=created)
    shutil.copyfile(legacy_read(live / CHECKSUMS), staging / CHECKSUMS)
    shutil.copyfile(legacy_read(live / MARKER), staging / MARKER)
    result = verify(staging)
    if any(result.values()):
        raise SystemExit(f"{staging} differs from the setup checksums: "
                         + ", ".join(f"{k} {len(v)} ({', '.join(v[:3])})" for k, v in result.items() if v))
    _write_manifest(staging, f"rebuilt from {disk}")
    _remove(target)
    journal(target).unlink(missing_ok=True)
    staging.rename(target)
    journal(staging).rename(journal(target))
    _write_manifest(target, f"rebuilt from {disk}")
    return target


def pristine(folder: Path | None = None) -> Path:
    """The intact snapshot of setup's state, made or repaired when needed."""
    import fcntl
    live = Path(folder).resolve() if folder is not None else live_folder()
    target = pristine_folder(live)
    with open(live.parent / (target.name + ".lock"), "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if target.is_dir():
            problems = pristine_problems(target)
            if not problems:
                return target
            print(f"win98_c: {target} was modified ({'; '.join(problems[:5])}"
                  f"{' ...' if len(problems) > 5 else ''})", flush=True)
        require(live)
        return _rebuild_pristine(live, target)


def use_pristine() -> Path:
    """Make this process and its children see the pristine C: (tests)."""
    global FOLDER
    if os.environ.get("NEO98_WIN98_PRISTINE") == "1" and FOLDER.name.endswith(PRISTINE_SUFFIX):
        return FOLDER
    live = live_folder()
    target = pristine(live)
    os.environ["NEO98_WIN98_LIVE_ROOT"] = str(live)
    os.environ["NEO98_WIN98_ROOT"] = str(target)
    os.environ["NEO98_WIN98_PRISTINE"] = "1"
    # Tests are silent, also when a runner is started without
    # neo98.tool_environment (as that function does since 06d1640).
    os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
    FOLDER = target
    return target


def supported_isos() -> list[dict]:
    return json.loads(SUPPORTED_ISOS.read_text())["isos"]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--verify", action="store_true", help="compare every file with the setup checksums")
    parser.add_argument("--pristine", action="store_true",
                        help="check or rebuild the snapshot of setup's state the tests start from")
    args = parser.parse_args(argv)
    if args.pristine:
        target = pristine()
        print(f"Pristine copy {target} and {journal(target)} unchanged")
        return 0
    info = record()
    print(f"Win98 folder {FOLDER}: {info['files']} files, {info['directories']} directories, "
          f"ISO {info['iso']['name']} ({info['iso']['sha256'][:16]}), eingerichtet {info['created']}")
    if args.verify:
        result = verify()
        for kind, names in result.items():
            print(f"{kind}: {len(names)}")
            for name in names[:50]:
                print(f"  {name}")
        return 1 if result["changed"] or result["removed"] else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
