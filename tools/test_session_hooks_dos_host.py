#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Hooks of the session reach the MS-DOS window and its property sheet.

In Windows 95 and 98 the MS-DOS window belongs to WINOLDAP, a task of the
system VM, and its property sheet runs in the same task. A 32-bit DLL that sets
WH_CBT, WH_CALLWNDPROC and WH_GETMESSAGE for the session is loaded into the
process of that task, and its procedures are called there for the window
(class tty: created, activated, focus, WM_SETFOCUS, its keys) and for the
sheet (created, WM_INITDIALOG, its fields created, the focus in the first
field, the keys typed there). Measured with the same two programs in a
real Windows 95 OSR2 and Windows 98 SE (docs/DOS-WINDOW-HOOKS.md).

The port ran the hooks in no process without an x86 processor, and the DOS
process has none until it loads an x86 module: no hook was called for the
window, and none for the sheet, also after COMCTL32 had brought the
processor. Now the hook DLL itself is the first x86 module of the DOS
process, and the loop of the window runs WH_GETMESSAGE.

tools/session_hook_dos_fixture.c (HOOKRUN.EXE) with
tools/session_hook_dos_dll_fixture.c (HOOKMEAS.DLL), on a copy of the
pristine drive C:

    build/analysis-venv/bin/python tools/test_session_hooks_dos_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):
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

FLAGS = ['-march=pentium', '-mno-sse', '-mno-mmx', '-Os', '-ffreestanding', '-fno-builtin', '-nostdlib',
         '-Wl,--major-subsystem-version,4', '-Wl,--minor-subsystem-version,0']


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('session-hooks-dos')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), out / 'c.registry')
    (drive / 'MEAS').mkdir(exist_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', *FLAGS, '-shared', str(ROOT / 'tools/session_hook_dos_dll_fixture.c'),
                    '-Wl,-e,_DllMain@12', '-lkernel32', '-luser32', '-o', str(drive / 'MEAS/HOOKMEAS.DLL')], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', *FLAGS, str(ROOT / 'tools/session_hook_dos_fixture.c'), '-Wl,-e,_start',
                    '-mwindows', '-lkernel32', '-luser32', '-o', str(drive / 'MEAS/HOOKRUN.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='200',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\MEAS\\HOOKRUN.EXE',
                        '--quiet', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=400)
    hooks = (drive / 'MEAS/HOOK.TXT').read_text(errors='replace').splitlines() if (drive / 'MEAS/HOOK.TXT').is_file() else []
    (out / 'HOOK.TXT').write_text('\n'.join(hooks))
    shutil.rmtree(drive, ignore_errors=True)

    def events(kind, klass, title=None, message=None):
        found = []
        for line in hooks:
            fields = dict(part.split('=', 1) for part in line.split(' ')[1:] if '=' in part)
            if (line.split(' ')[0] == kind and fields.get('class') == klass and
                    (title is None or line.endswith('title=' + title)) and
                    (message is None or fields.get('msg') == message)):
                found.append(fields.get('pid'))
        return found
    window = events('CBT-CREATE', 'tty')
    process = window[0] if window else None
    def here(found):
        return bool(found) and all(pid == process for pid in found)
    checks = {
        'the DLL is loaded into the process of the MS-DOS window':
            process is not None and any(line.startswith(f'ATTACH pid={process} ') for line in hooks),
        'WH_CBT: the window is created there': here(window),
        'WH_CBT: the window is activated and gets the focus there':
            here(events('CBT-ACTIVATE', 'tty', 'MS-DOS Prompt')) and here(events('CBT-FOCUS', 'tty', 'MS-DOS Prompt')),
        'WH_CALLWNDPROC: WM_SETFOCUS of the window': here(events('CALLWND', 'tty', 'MS-DOS Prompt', '7')),
        'WH_GETMESSAGE: the input of the window': here(events('GETMSG', 'tty', 'MS-DOS Prompt')),
        'WH_CBT: the property sheet and its fields are created in the same process':
            here(events('CBT-CREATE', '#32770')) and here(events('CBT-CREATE', 'Edit')),
        'WH_CALLWNDPROC: WM_INITDIALOG of the sheet': here(events('CALLWND', '#32770', None, '110')),
        'WH_CBT: the sheet is activated, a field of it gets the focus':
            here(events('CBT-ACTIVATE', '#32770', 'MS-DOS Prompt Properties')) and here(events('CBT-FOCUS', 'Edit')),
        'WH_GETMESSAGE: the keys typed into the field':
            here(events('GETMSG', 'Edit', None, '100')) and here(events('GETMSG', 'Edit', None, '102')),
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    hooks:', out / 'HOOK.TXT', ' log:', out / 'log.txt')
    print(f'session hooks dos host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
