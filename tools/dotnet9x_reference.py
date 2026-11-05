#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The three console programs of dotnet9x that two host tests run.

build/dotnet9x-reference used to be a hand-made checkout of MattKC's dotnet9x
(https://github.com/itsmattkc/dotnet9x, GPL-3.0) at the commit named in
 It does not come from the Win98 ISO, so a rebuilt build/
has none. tools/test_bspatch_native.py and tools/test_console_codecs_native.py
need three files of its `patches` folder and nothing else; this module fetches
exactly those from the pinned commit, checks them against the pinned SHA-256
and leaves them where the checkout had them.

Without network and without the files the tests skip with a note, like the
tests of the DirectX 9 tree do without their package (tools/dx9_tree.py).

    build/analysis-venv/bin/python tools/dotnet9x_reference.py
"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'e7086ddc74a7f92ea12bc6f480a3637337fd5c71'
URL = 'https://raw.githubusercontent.com/itsmattkc/dotnet9x/' + COMMIT + '/patches/'
PATCHES = ROOT / 'build/dotnet9x-reference/patches'
# bzip2.exe and bunzip2.exe are one binary under two names, as upstream ships it.
FILES = {
    'bspatch.exe': '53e9f4790c3adf81137c279a37e0811c9cc0220b3cc38af00a45a3b6aadae59e',
    'bzip2.exe': 'daa4b76e4b63116f2a88a1c54fb407efa7b988b02d3d3d24cf1b7bddad0786ed',
    'bunzip2.exe': 'daa4b76e4b63116f2a88a1c54fb407efa7b988b02d3d3d24cf1b7bddad0786ed',
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def missing():
    return [name for name, pinned in FILES.items()
            if not (PATCHES / name).is_file() or digest(PATCHES / name) != pinned]


def fetch():
    """Download what is missing; raises OSError without network."""
    PATCHES.mkdir(parents=True, exist_ok=True)
    for name in missing():
        with urllib.request.urlopen(URL + name, timeout=60) as response:
            data = response.read()
        found = hashlib.sha256(data).hexdigest()
        if found != FILES[name]:
            raise OSError(f'{name} from {URL} has SHA-256 {found}, pinned is {FILES[name]}')
        # Two tests may fetch at the same time: write beside and rename.
        partial = PATCHES / f'{name}.{os.getpid()}.partial'
        partial.write_bytes(data)
        partial.replace(PATCHES / name)


def skip_reason():
    """None when the three files are in place, else why they are not."""
    if not missing():
        return None
    try:
        fetch()
    except OSError as error:
        return (f'build/dotnet9x-reference/patches is incomplete and could not be fetched ({error}); '
                'run tools/dotnet9x_reference.py with network access')
    return None


def require():
    """For a test's first line: the folder, or exit 0 with a note."""
    reason = skip_reason()
    if reason:
        print(f'SKIP: {reason}', flush=True)
        raise SystemExit(0)
    return PATCHES


if __name__ == '__main__':
    reason = skip_reason()
    print(reason or f'{PATCHES.relative_to(ROOT)}: {", ".join(FILES)} at {COMMIT[:12]}')
    sys.exit(1 if reason else 0)
