#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DOS3Call 4Eh with the label bit among other attribute bits.

tools/find_attributes_win16_fixture.asm searches with CX = 1Fh, the way
SETUPX looks for C:\\WINDOWS\\SETUP.INI: a name that does not exist must fail,
an existing one must be found under its name. The program writes one letter
per check into C:\\FIND16.TXT.

    build/analysis-venv/bin/python tools/test_find_attributes_win16_host.py [--no-build]
"""
from __future__ import annotations

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


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('find-attributes-win16')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-c', '-R', str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/find_attributes_win16_fixture.asm', out / 'FIND16.EXE', 'FIND16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'FIND16.EXE', drive / 'FIND16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\FIND16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\FIND16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    checks = {
        'CX = 1Fh, a name that does not exist: carry and error 2 or 18': data[:1] == b'a',
        'CX = 1Fh, WIN.INI: found under its own name': data[1:2] == b'b',
    }
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print('written:', data, '\n    log:', out / 'log.txt')
        return 1
    print('find attributes: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
