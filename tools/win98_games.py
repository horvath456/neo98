#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The accessory games of Win98 for a test: WINMINE.EXE, SOL.EXE, FREECELL.EXE
and CARDS.DLL out of the cabinets of the ISO.

The express setup of tools/setup_win98.py installs no games,
so drive C: has none. The ISO is the one setup used: NEO98_WIN98_ISO, or a file
of its size in ~/Downloads. Without the ISO, 7zz or cabextract the answer is
None and the caller skips. All four programs are 16-bit (NE).
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402

FILES = ('WINMINE.EXE', 'SOL.EXE', 'FREECELL.EXE', 'CARDS.DLL')


def iso():
    named = os.environ.get('NEO98_WIN98_ISO')
    if named:
        return Path(named) if Path(named).exists() else None
    try:
        size = win98_c.record()['iso']['size']
    except (SystemExit, OSError, KeyError, ValueError):
        return None
    for candidate in sorted((Path.home() / 'Downloads').glob('*.iso')):
        if candidate.stat().st_size == size:
            return candidate
    return None


def folder(out):
    """A folder with the games in upper case names, made once under out."""
    games = Path(out) / 'win98-games'
    if all((games / name).exists() for name in FILES):
        return games
    source = iso()
    tools = shutil.which('7zz') or shutil.which('7z'), shutil.which('cabextract')
    if not source or not all(tools):
        print('Win98 games skipped: ' + ('the ISO of setup was not found (NEO98_WIN98_ISO)' if not source
                                         else '7zz or cabextract missing'), flush=True)
        return None
    scratch = Path(out) / 'win98-games-scratch'
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    games.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run([tools[0], 'x', '-y', f'-o{scratch}', str(source), 'win98/WIN98_*.CAB', 'WIN98/WIN98_*.CAB'],
                       capture_output=True)
        cabinets = sorted(scratch.rglob('WIN98_*.CAB'))
        if not cabinets:
            print('Win98 games skipped: the ISO holds no WIN98_*.CAB', flush=True)
            return None
        subprocess.run([tools[1], '-q', '-d', str(scratch / 'out'), *sum((['-F', name] for name in FILES), []),
                        *map(str, cabinets)], capture_output=True)
        for found in (scratch / 'out').iterdir():
            if found.name.upper() in FILES:
                shutil.copyfile(found, games / found.name.upper())
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if not all((games / name).exists() for name in FILES):
        print('Win98 games skipped: the cabinets do not hold all of ' + ', '.join(FILES), flush=True)
        return None
    return games


if __name__ == '__main__':
    print(folder(Path(__file__).resolve().parents[1] / 'build/state-host'))
