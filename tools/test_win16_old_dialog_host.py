#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Calls of KERNEL and USER that the old-style file dialog of a 16-bit
COMMDLG makes, from a program of its own.

tools/old_dialog_win16_fixture.asm writes one letter per check into
C:\\OLDDLG16.TXT, "-" for a check that failed, as soon as the check is done.

    build/analysis-venv/bin/python tools/test_win16_old_dialog_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

CHECKS = [
    'a LoadResource again while loaded: the same handle, the changed byte',
    'b LoadResource after the last FreeResource: the bytes of the file again',
]


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-old-dialog')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    win16_fixture.assemble(ROOT / 'tools/old_dialog_win16_fixture.asm', out / 'OLDDLG16.EXE', 'OLDDLG16',
                           neo98.tool_environment(), resources=[(10, 1, b'ORIGINAL')])
    shutil.copyfile(out / 'OLDDLG16.EXE', drive / 'OLDDLG16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\OLDDLG16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\OLDDLG16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    failed = 0
    for index, name in enumerate(CHECKS):
        okay = data[index:index + 1] == name[:1].encode()
        failed += not okay
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if failed:
        print('written:', data, '\n    log:', out / 'log.txt')
        return 1
    print('win16 old dialog: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
