#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the ways of starting a program answer when it does not start.

tools/start_errors_fixture.c calls ShellExecuteA, ShellExecuteExA (hInstApp
and GetLastError), WinExec, CreateProcessA (with the command line and with
the application name), LoadModule and GetFileAttributesA for files that are
missing, in a folder that is missing, no program, a document without a
program, a folder, open by another program, and programs whose DLL or one
of whose imports is missing. The expected answers are those of Windows 95
OSR2 and Windows 98 SE, measured with the same program; both answer alike
in every case here. Then it opens the Run dialog of the shell for three of
them; the log of the run has the messages Windows shows:

  a missing folder   "Cannot find the file '...' (or one of its components)",
                     the same as a missing file: ShellExecute answers 2
  no program         "... is not a valid Win32 application."
                     (ERROR_BAD_EXE_FORMAT, 193, also from CreateProcess)
  a missing import   KERNEL32's "The MISSEXP.EXE file is linked to missing
                     export KERNEL32.DLL:GetModuleFileNameQ.", then the
                     shell's box with the file and, after an empty line,
                     "A device attached to the system is not functioning."
                     (ERROR_GEN_FAILURE, 31)

The port started a program with an import its own KERNEL32 did not know
(the program stopped at the first call of it) and answered 2 for a folder
given as the program, where Windows answers 5.

    build/analysis-venv/bin/python tools/test_start_errors_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
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
import win98_c  # noqa: E402

# Windows 95 OSR2 and Windows 98 SE; a result above 32 is written as 33.
EXPECTED = {
    'missing-file': 'GFA ffffffff/2|SE 2/2|SEX 0,2/2|WE 2/2|CP 0/2|CPA 0/2|LM 2/2',
    'missing-folder': 'GFA ffffffff/3|SE 2/2|SEX 0,2/2|WE 2/2|CP 0/2|CPA 0/2|LM 3/3',
    'missing-name': 'GFA ffffffff/2|SE 2/2|SEX 0,2/2|WE 2/2|CP 0/2|CPA 0/2|LM 2/2',
    'text-exe': 'GFA 20/0|SE 5/193|SEX 0,5/193|WE 0/193|CP 0/193|CPA 0/193|LM 0/193',
    'empty-exe': 'GFA 20/0|SE 5/193|SEX 0,5/193|WE 0/193|CP 0/193|CPA 0/193|LM 0/193',
    'no-association': 'GFA 20/0|SE 31/1155|SEX 0,31/1155|WE 0/193|CP 0/193|CPA 0/193|LM 0/193',
    'folder': 'GFA 10/0|WE 5/5|CP 0/5|CPA 0/5|LM 5/5',
    'locked-exe': 'GFA 20/0|SE 26/32|SEX 0,26/32|WE 0/32|CP 0/32|CPA 0/32|LM 0/32',
    'missing-dll': 'GFA 20/0|SE 32/1157|SEX 0,32/1157|WE 0/1157|CP 0/1157|CPA 0/1157|LM 0/1157',
    'missing-export': 'GFA 20/0|SE 5/31|SEX 0,5/31|WE 0/31|CP 0/31|CPA 0/31|LM 31/31',
    'program': 'WE 33/0|CP 1/0',
}
MESSAGES = [
    ('missing folder', "Cannot find the file 'C:\\SE\\NODIR\\NOFILE.EXE' (or one of its components)"),
    ('no program', 'C:\\SE\\TEXT.EXE is not a valid Win32 application.'),
    ('missing import, KERNEL32', 'The MISSEXP.EXE file is\nlinked to missing export KERNEL32.DLL:GetModuleFileNameQ.'),
    ('missing import, the shell', 'C:\\SE\\MISSEXP.EXE\n\nA device attached to the system is not functioning.'),
]


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('start-errors')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/start_errors_fixture.c'), '-ffreestanding',
                    '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lkernel32', '-luser32', '-lshell32', '-o', str(drive / 'STARTERR.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='300',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\STARTERR.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=420)
    written = win98_c.path('C:\\SE\\RESULT.TXT', drive)
    lines = written.read_text(errors='replace').splitlines() if written else []
    got = {line.split('|', 1)[0]: line.split('|', 1)[1] for line in lines if '|' in line}
    logged = (out / 'log.txt').read_text(errors='replace').replace('\r', '')
    shutil.rmtree(drive, ignore_errors=True)
    checks = {'the fixture went through all cases': 'done' in lines}
    for case, expected in EXPECTED.items():
        checks[f'{case}: {got.get(case)} (Windows: {expected})'] = got.get(case) == expected
    for name, text in MESSAGES:
        checks[f'Run, {name}: "{text[:60]}..." shown'] = text in logged
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'start errors host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
