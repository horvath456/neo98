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
answers a number above 32 and the window comes.

tools/start_answers_win16_fixture.asm (ANSWER16.EXE) then measures the
answers, the same program as in Windows 95 OSR2 and Windows 98 SE under
QEMU, which answered alike:

  a 16-bit program    WinExec and LoadModule (KERNEL.45) answer its
                      instance, ShellExecute 42 (SHELL32 has no instance of
                      a program that has not run yet)
  a 32-bit program    an instance (Windows: that of the 16-bit module of
                      the process; this port has none and answers a word of
                      its own, never 42); GetModuleUsage of it is 1 while
                      the program runs and 0 after
  a missing drive     WinExec 3, LoadModule 3, ShellExecute 2; the same
  or folder           for a missing folder, 2 for a missing file
  OpenFile            -1 and 3 for a missing drive and a missing folder
  DOS3Call, Q:        3 from open, change directory, find first, make
                      directory, the attributes and the long-name
                      attributes; FFFFh from the free space (36h); carry and
                      15 from the current directory (47h)

The port answered 42 (or another number) for every program, 2 for a
missing drive or folder, 2 from find first and the attributes, and the
current directory of a drive that does not exist.

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
# The lines of ANSWER16 that are the same in Windows 95 and 98 word for word.
ANSWER_LINES = [
    'missing-drive|WE 3|LM 3|SE 2',
    'missing-folder|WE 3|LM 3|SE 2',
    'missing-file|WE 2|LM 2|SE 2',
    'openfile|drive -1,3|folder -1,3',
    'dos|3D 3,1|3B 3,1|4E 3,1|39 3,1|4300 3,1|7143 3,1|36 ffff,0|47 f,1',
]


def fields(line):
    return dict(part.split(' ', 1) for part in line.split('|')[1:] if ' ' in part)


def run_host(binary, drive, out, program, deadline):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE=str(deadline),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / f'log-{program.split(chr(92))[-1]}.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', program, '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=deadline + 120)


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
    (drive / 'S16').mkdir(exist_ok=True)
    for name in ('ANSWER16', 'CHILD16'):
        win16_fixture.assemble(ROOT / 'tools/start_answers_win16_fixture.asm', out / f'{name}.EXE', name,
                               neo98.tool_environment(), ('KERNEL', 'USER', 'SHELL'), stack=0x2000)
        shutil.copyfile(out / f'{name}.EXE', drive / 'S16' / f'{name}.EXE')
    run_host(binary, drive, out, 'C:\\START16.EXE', 120)
    run_host(binary, drive, out, 'C:\\S16\\ANSWER16.EXE', 200)
    written = win98_c.path('C:\\START16.TXT', drive)
    data = written.read_bytes() if written else b''
    written = win98_c.path('C:\\S16\\RESULT.TXT', drive)
    lines = written.read_text(errors='replace').splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    words = struct.unpack(f'<{len(data) // 2}H', data[:len(data) // 2 * 2]) if data else ()
    checks = {'the program wrote its eight words': len(words) == 8}
    for i, name in enumerate(STARTS):
        answer, came = (words[2 * i], words[2 * i + 1]) if len(words) == 8 else (None, None)
        checks[f'{name}: answer {answer}, the window came {came}'] = answer is not None and answer > 32 and came == 1
    got = {line.split('|', 1)[0]: line for line in lines}
    checks['ANSWER16 went through all its lines'] = 'done' in lines
    for way in ('winexec', 'loadmodule', 'shellexecute'):
        f = fields(got.get(f'child-{way}', ''))
        answer, instance = int(f.get('answer', '0'), 16), int(f.get('instance', '0'), 16)
        want = 42 if way == 'shellexecute' else instance
        checks[f'a 16-bit program through {way}: answer {answer:#x}, its instance {instance:#x} (Windows: '
               f'{"42" if way == "shellexecute" else "its instance"})'] = instance > 32 and answer == want
        f = fields(got.get(f'calc-{way}', ''))
        answer = int(f.get('answer', '0'), 16)
        checks[f'Calculator through {way}: answer {answer:#x}, window {f.get("window")}, GetModuleUsage while it '
               f'runs and after {f.get("usage")} (Windows: an instance, 1,0)'] = (
            answer > 32 and answer != 42 and f.get('window') == '1' and f.get('usage') == '1,0')
    for line in ANSWER_LINES:
        name = line.split('|', 1)[0]
        checks[f'{name}: {got.get(name)} (Windows: {line})'] = got.get(name) == line
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    logs:', out)
    print(f'start win16 host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
