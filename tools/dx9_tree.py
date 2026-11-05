#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A copy of the Win98 folder with DirectX 9.0c, for the D3D8/D3D9/DirectDraw driver tests.

The Win98 folder of setup is the vanilla installation with DirectX 6.1. The
driver tests measure against the DirectX 9.0c originals, so they get a copy of
their own: an APFS clone of the folder under build/dx9-tree/win98-c, with the
runtime of the pinned redistributable put into WINDOWS\\SYSTEM. No DirectX
file ever goes into the Win98 folder itself; users install runtimes
themselves, as on a real PC.

The real DXSETUP.EXE followed by a start of the machine (WININIT.INI) installs the same package; tools/test_wininit_native.py does that.
The driver tests take the files out of DirectX.cab with cabextract instead,
the way the October 2006 package ships them, because it is faster. Without
the package the tests that need it skip with a note.

    build/analysis-venv/bin/python tools/dx9_tree.py # build or reuse the copy
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c    # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = Path(os.environ.get("NEO98_DX9_REDIST") or
               ROOT / "build/external/set-NEO98_DX9_REDIST")
PACKAGE_SHA256 = "daa0c3e48b44356a58ec7639d0b646e0f9fc476ad680d0a92ccd771b5be76891"
OUT = ROOT / "build/dx9-tree"
TREE = OUT / "win98-c"
MARKER = ".neo98-dx9.json"
# What DirectX.cab puts into WINDOWS\SYSTEM on Windows 98.
SYSTEM_SUFFIXES = (".dll", ".exe", ".vxd", ".drv", ".cpl", ".ax")


def skip_reason():
    """Why the DirectX 9 copy cannot exist, or None."""
    if not win98_c.ready():
        return win98_c.NOT_READY.format(folder=win98_c.FOLDER)
    if not PACKAGE.is_file():
        return f"DirectX 9.0c redistributable missing ({PACKAGE}); set NEO98_DX9_REDIST"
    if not shutil.which("cabextract"):
        return "cabextract missing; brew install cabextract"
    return None


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as data:
        for chunk in iter(lambda: data.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tree():
    """The DirectX 9 copy of the Win98 folder, built on first use, or None with a note."""
    reason = skip_reason()
    if reason:
        print(f"DirectX 9 tree skipped: {reason}", flush=True)
        return None
    setup = win98_c.record()
    marker = TREE / MARKER
    expected = {"package_sha256": PACKAGE_SHA256, "folder_created": setup["created"]}
    if marker.is_file() and json.loads(marker.read_text()).get("key") == expected:
        return TREE
    if _sha256(PACKAGE) != PACKAGE_SHA256:
        raise SystemExit(f"{PACKAGE} is not the pinned DirectX 9.0c October 2006 redistributable")
    if TREE.is_symlink():
        TREE.unlink()  # an earlier copy that was only a link into main
    if TREE.exists():
        subprocess.run(["chmod", "-R", "u+w", str(TREE)], check=True)
        shutil.rmtree(TREE)
    OUT.mkdir(parents=True, exist_ok=True)
    # An APFS clone costs no blocks until a file changes. In a worktree
    # build/win98-c is a symlink into the main tree; cp -R copies a symlink
    # operand as the link, and the DirectX files below then went into the
    # shared Win98 folder itself. Clone the folder it points to.
    subprocess.run(["cp", "-c", "-R", str(win98_c.FOLDER.resolve()), str(TREE)], check=True)
    if TREE.is_symlink() or not TREE.is_dir():
        raise SystemExit(f"{TREE} is no copy of the Win98 folder; refusing to write DirectX files into it")
    system = win98_c.path("WINDOWS/SYSTEM", TREE)
    installed = []
    with tempfile.TemporaryDirectory(dir=OUT) as scratch:
        scratch = Path(scratch)
        subprocess.run(["cabextract", "-q", "-d", str(scratch), "-F", "DirectX.cab", str(PACKAGE)], check=True)
        files = scratch / "files"
        subprocess.run(["cabextract", "-q", "-d", str(files), str(scratch / "DirectX.cab")], check=True,
                       stderr=subprocess.DEVNULL)
        for source in sorted(files.iterdir()):
            if not source.name.lower().endswith(SYSTEM_SUFFIXES):
                continue
            existing = win98_c.path(f"WINDOWS/SYSTEM/{source.name}", TREE)
            target = existing if existing is not None else system / source.name.upper()
            if target.exists():
                target.chmod(0o644)
                target.unlink()
            shutil.copyfile(source, target)
            installed.append(target.name)
    marker.write_text(json.dumps({"key": expected, "package": str(PACKAGE), "files": installed},
                                 indent=2) + "\n")
    print(f"DirectX 9 tree {TREE}: {len(installed)} files of DirectX.cab in WINDOWS\\SYSTEM", flush=True)
    return TREE


if __name__ == "__main__":
    sys.exit(0 if tree() else 1)
