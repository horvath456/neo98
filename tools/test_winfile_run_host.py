#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""File Manager's Run: what is typed replaces the text the dialog put there.

The Run dialog of File Manager (WINFILE.EXE, 16-bit) puts the selected file
into its Command Line field. The dialog manager gives the first field the
focus the way it always does (DlgSetFocus): an edit field first gets its
whole text selected (EM_SETSEL 0, -1), so what the user types replaces it.
The port gave the focus without the selection: what was typed went in
before the file ("freecellC:\\"), and File Manager answered "cannot find
the specified file".

tools/winfile_run_fixture.c starts WINFILE.EXE, chooses File > Run and
looks at the field: its whole text is selected, and one typed character
leaves a text of one character. Then "calc" and "c:\\windows\\calc.exe",
typed into the field and confirmed with OK, start Calculator. Measured
with the same program in Windows 95 OSR2 and Windows 98 SE.

    build/analysis-venv/bin/python tools/test_winfile_run_host.py [--no-build]
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


def main():
    pristine = win98_c.use_pristine()
    if not win98_c.path('C:\\WINDOWS\\WINFILE.EXE', pristine):
        print('SKIP: the drive has no WINFILE.EXE')
        return 77
    out = neo98.output_directory('winfile-run')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), out / 'c.registry')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/winfile_run_fixture.c'), '-ffreestanding',
                    '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(drive / 'WFRUN.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='100',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\WFRUN.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\WFRUN.TXT', drive)
    lines = written.read_text(errors='replace').splitlines() if written else []
    got = dict(line.split('=', 1) for line in lines if '=' in line)
    shutil.rmtree(drive, ignore_errors=True)
    length = got.get('length', '')
    checks = {
        'File Manager shows its Run dialog with the Command Line field':
            got.get('frame') == '1' and got.get('dialog') == '1' and got.get('field') == '1',
        f'the field holds the selected file ({length} characters)': length.isdigit() and int(length) > 0,
        f"its whole text is selected: {got.get('selection.start')} to {got.get('selection.end')}":
            got.get('selection.start') == '0' and got.get('selection.end') == length,
        f"a typed character replaces it: {got.get('typed.length')} character":
            got.get('typed.length') == '1',
        'Run "calc": Calculator comes': got.get('started by name') == '1',
        'Run "c:\\windows\\calc.exe": Calculator comes': got.get('started by path') == '1',
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'winfile run host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
