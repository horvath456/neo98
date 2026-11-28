#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A 16-bit program starts a program by its name and by its path.

File Manager's Run (WINFILE.EXE, 16-bit) starts what was typed through
SHELL's ShellExecute; other 16-bit programs use KERNEL's WinExec. Both find
a bare name the way Windows does: in the current directory, the Windows
and System directories and along PATH.

tools/start_win16_fixture.asm starts Calculator four times: "calc" and
"c:\\windows\\calc.exe" through ShellExecute (SHELL.20) and through WinExec
(KERNEL.166), each time waiting for its window and closing it. Every call
answers a number above 32 and the window comes, as in Windows 95 and 98
with the same program.

    build/analysis-venv/bin/python tools/test_start_win16_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('nasm'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs nasm')
        _sys.exit(77)

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

STARTS = ['ShellExecute "calc"', 'ShellExecute "c:\\windows\\calc.exe"', 'WinExec "calc"',
          'WinExec "c:\\windows\\calc.exe"']


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('start-win16')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), out / 'c.registry')
    win16_fixture.assemble(ROOT / 'tools/start_win16_fixture.asm', out / 'START16.EXE', 'START16',
                           neo98.tool_environment(), ('KERNEL', 'USER', 'SHELL'))
    shutil.copyfile(out / 'START16.EXE', drive / 'START16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='120',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\START16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\START16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    words = struct.unpack(f'<{len(data) // 2}H', data[:len(data) // 2 * 2]) if data else ()
    checks = {'the program wrote its eight words': len(words) == 8}
    for i, name in enumerate(STARTS):
        answer, came = (words[2 * i], words[2 * i + 1]) if len(words) == 8 else (None, None)
        checks[f'{name}: answer {answer}, the window came {came}'] = answer is not None and answer > 32 and came == 1
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'start win16 host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
