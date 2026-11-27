#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""CTRL+TAB and its kin in a property sheet.

COMCTL32's property sheet asks its own IsDialogMessage before USER's, in the
loop of a modal PropertySheet and in PSM_ISDIALOGMESSAGE, which a program
with a modeless sheet calls: a key going down with CTRL held, TAB (with
SHIFT backwards), PAGE UP (backwards) or PAGE DOWN, turns to the next or
previous page, round the ends. Neo98's modal loop gave such a key to USER,
which moved the focus as for TAB, and PSM_ISDIALOGMESSAGE ended the run as
an unported path.

tools/sheet_keys_fixture.c shows three pages that write down PSN_SETACTIVE
and the focus of their edit fields, once modal and once modeless.

    build/analysis-venv/bin/python tools/test_sheet_keys_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
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

# CTRL+TAB, CTRL+SHIFT+TAB, CTRL+PAGE DOWN, CTRL+PAGE UP twice (the second
# goes round to the last page), CTRL+TAB (round to the first), TAB, ESC.
STEPS = ['1:ctrl=9', '2:keydown=10', '2.2:ctrl=9', '2.4:keyup=10', '3.5:ctrl=22', '4.5:ctrl=21', '5.5:ctrl=21',
         '6.5:ctrl=9', '7.5:key=9', '8.5:key=1b', '11:menu=quit']
PAGES = [0, 1, 0, 1, 0, 2, 0]


def run(binary, pristine, out, name, argument, log_name):
    folder = out / name
    folder.mkdir(parents=True)
    drive = folder / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    shutil.copyfile(out / 'SHEETKEY.EXE', drive / 'SHEETKEY.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US',
               NEO98_HOST_DEADLINE='30', NEO98_HOST_INPUT=';'.join(['0:wait=|Sheet keys', *STEPS]),
               NEO98_SETTINGS_DIR=str(folder / 'settings'), NEO98_LOG_DIR=str(folder / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = ['--command', 'C:\\SHEETKEY.EXE ' + argument] if argument else []
    with (folder / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\SHEETKEY.EXE', *command,
                        '--quiet', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(folder / 'c.registry'), '--crash-report-dir', str(folder / 'reports')],
                       cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\' + log_name, drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    return lines


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('sheet-keys')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/sheet_keys_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-lcomctl32',
                    '-o', str(out / 'SHEETKEY.EXE')], env=neo98.tool_environment(), check=True)
    checks, logs = {}, {}
    for name, argument, log_name in (('modal', '', 'SHEETKEY.TXT'), ('modeless', 'm', 'SHEETMOD.TXT')):
        lines = run(binary, pristine, out, name, argument, log_name)
        logs[name] = lines
        pages = [int(line.split()[1]) for line in lines if line.startswith('page')]
        after = lines[lines.index('page 0', len(lines) - 4):] if lines.count('page 0') else []
        checks[f'{name}: the sheet wrote its log when it closed'] = bool(lines)
        checks[f'{name}: CTRL+TAB, CTRL+SHIFT+TAB, CTRL+PAGE DOWN and UP turn the pages: {pages}'] = pages == PAGES
        checks[f'{name}: TAB alone moves the focus on the page and keeps the page'] = \
            lines[-1:] == ['left 0'] and pages[-1:] == [0] and after.count('page 0') == 1
    (out / 'results.json').write_text(json.dumps({'logs': logs, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    logs:', logs, '\n    folders in', out)
    print(f'sheet keys host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
