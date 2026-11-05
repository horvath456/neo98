#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The tests start from the C: setup produced, not from the user's machine.

build/win98-c and its journal are drive C: of the user: the host writes the
user's files into the folder and every registry change, among them the
per-user Active Setup steps, into the journal beside it. tools/win98_c.py
keeps a snapshot of setup's state beside it (win98-c.setup and its journal),
and use_pristine points the tests at it.

1. The real snapshot: every file matches the setup checksums, nothing was
   added, and the journal holds only what setup wrote (the cleanup records and
   the time zone), so no HKCU Active Setup record of a first shell start.
2. use_pristine sets FOLDER and NEO98_WIN98_ROOT, and a second call keeps them.
3. On a scratch clone: snapshot_pristine records the manifest, and a changed
   file, an added file, a removed file and a journal record are each found.
4. pristine of a scratch folder without the installed disk image refuses to
   hand out a changed snapshot.

    build/analysis-venv/bin/python tools/test_win98_pristine.py
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import win98_c  # noqa: E402
import win98_folder  # noqa: E402

OUT = ROOT / 'build/win98-pristine'
CHECKS = []


def check(name, ok, detail=''):
    CHECKS.append(bool(ok))
    print(('ok   ' if ok else 'FAIL ') + name + (f': {detail}' if detail else ''), flush=True)


def journal_paths(data):
    """The key paths the records of a change journal name, in order."""
    assert data.startswith((win98_folder.JOURNAL_SIGNATURE, bytes([65, 82, 77, 57, 56]) + b'REG1'))
    text = data.decode('latin-1')
    return [k for k in ('Active Setup', 'TimeZoneInformation', 'CurrentVersion\\Run') if k in text]


def part_real():
    live = win98_c.live_folder()
    target = win98_c.pristine()
    check('the snapshot lies beside the live folder', target == win98_c.pristine_folder(live)
          and target.parent == live.parent, str(target))
    result = win98_c.verify(target)
    check('every file of the snapshot matches the setup checksums',
          not any(result.values()), {k: v[:3] for k, v in result.items() if v})
    data = win98_c.journal(target).read_bytes()
    named = journal_paths(data)
    check('the journal of the snapshot has the cleanup and the time zone',
          'CurrentVersion\\Run' in named and 'TimeZoneInformation' in named, str(named))
    check('the journal of the snapshot has no Active Setup record of a shell start',
          'Active Setup' not in named, f'{len(data)} bytes')
    check('the snapshot is intact', not win98_c.pristine_problems(target))


def part_environment():
    folder = win98_c.use_pristine()
    check('use_pristine points FOLDER at the snapshot', win98_c.FOLDER == folder)
    check('use_pristine sets NEO98_WIN98_ROOT for the runners', os.environ['NEO98_WIN98_ROOT'] == str(folder))
    check('a second call keeps the snapshot', win98_c.use_pristine() == folder)
    check('the live folder stays known', win98_c.live_folder() != folder)


def part_scratch():
    if OUT.exists():
        subprocess.run(['chmod', '-R', 'u+w', str(OUT)], check=True)
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    live = OUT / 'c'
    source = win98_c.pristine_folder(win98_c.live_folder())
    subprocess.run(['cp', '-c', '-R', str(source) + '/', str(live)], check=True)
    shutil.copyfile(win98_c.journal(source), win98_c.journal(live))
    target = win98_c.snapshot_pristine(live)
    check('snapshot_pristine clones folder and journal', target == OUT / 'c.setup'
          and win98_c.journal(target).is_file() and not win98_c.pristine_problems(target))
    # The live folder of the scratch is the user's: changing it leaves the snapshot alone.
    (live / 'My Documents').mkdir(exist_ok=True)
    (live / 'My Documents/letter.txt').write_text('user data')
    check('a change of the live folder leaves the snapshot intact', not win98_c.pristine_problems(target))
    win_ini = target / 'WINDOWS/WIN.INI'
    original = win_ini.read_bytes()
    win_ini.write_bytes(original + b'[Beetris]\r\n')
    problems = win98_c.pristine_problems(target)
    check('a changed file of the snapshot is found', 'changed WINDOWS/WIN.INI' in problems, str(problems[:3]))
    win_ini.write_bytes(original)
    (target / 'WINDOWS/Beetris.ini').write_text('x')
    check('an added file is found', 'added WINDOWS/Beetris.ini' in win98_c.pristine_problems(target))
    (target / 'WINDOWS/Beetris.ini').unlink()
    (target / 'WINDOWS/WININIT.INI').unlink(missing_ok=True)
    (target / 'CONFIG.SYS').unlink()
    check('a removed file is found', 'removed CONFIG.SYS' in win98_c.pristine_problems(target))
    shutil.rmtree(target)
    target = win98_c.snapshot_pristine(live)
    with open(win98_c.journal(target), 'ab') as journal:
        journal.write(b'\x03')
    check('a record added to the journal is found',
          any(p.startswith('journal') for p in win98_c.pristine_problems(target)))
    try:
        win98_c.pristine(live)
        refused = False
    except SystemExit as stop:
        refused = 'disk.raw' in str(stop)
    check('without the installed image a changed snapshot is refused', refused)
    subprocess.run(['chmod', '-R', 'u+w', str(OUT)], check=True)
    shutil.rmtree(OUT)


def main():
    win98_c.require(win98_c.live_folder())
    part_real()
    part_environment()
    part_scratch()
    passed = sum(CHECKS)
    print(f'WIN98 PRISTINE {"PASS" if passed == len(CHECKS) else "FAIL"} {passed}/{len(CHECKS)}')
    return 0 if passed == len(CHECKS) else 1


if __name__ == '__main__':
    sys.exit(main())
