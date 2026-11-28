#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""0313h to a window in the background brings it to the front first.

USER's DefWindowProc of 0313h, the system menu the taskbar asks for,
compares the window's queue with the foreground queue before it opens
anything: a window of another queue is made the foreground window by the
routine of SetForegroundWindow, and when that refuses nothing opens. Then the
menu opens at the point of the click, or with lParam -1 beside the window's
taskbar button (docs/SYSTEM-MENU.md). Neo98 opened the menu over a window
that stayed in the background, neither foreground nor active.

tools/popup_menu_foreground_fixture.c makes its own window the foreground
window and sends a window of another process 0313h with SendMessageCallback
as the taskbar does, once with a point and once with -1, and notes where the
foreground is and what the other window saw when its menu opened. EXPECTED
is what Windows 95 OSR2 and Windows 98 SE answer, measured with the same
program in a virtual machine. There Explorer's taskbar answers the shell
hook, so the form with -1 opens a second menu; here, without Explorer, it
opens none (test_taskbar_system_menu_host.py has that form with the
taskbar), and only the foreground is compared for it.

    build/analysis-venv/bin/python tools/test_popup_menu_foreground_host.py [--no-build]
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

EXPECTED = {
    'started': '1', 'window': '1', 'before.front': '1',
    'point.answered': '1', 'point.back': '1', 'point.front': '0', 'menu1.foreground': '1', 'menu1.active': '1',
    'between.front': '1',
    'keyboard.answered': '1', 'keyboard.back': '1', 'keyboard.front': '0',
    'done': '1',
}
WHAT = {
    'point.back': 'after the point form the window is the foreground window',
    'menu1.foreground': 'its menu opened while it was the foreground window',
    'menu1.active': 'and the active window of its thread',
    'keyboard.back': 'the form with -1 brings it to the front too',
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('popup-menu-foreground')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/popup_menu_foreground_fixture.c'),
                    '-ffreestanding', '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup',
                    '-mwindows', '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'POPFG.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='90',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\POPFG.EXE', '--quiet',
                        '--desktop', '800x600', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
    written = win98_c.path('C:\\POPFG.TXT', drive)
    got = dict(line.split('=', 1) for line in (written.read_text(errors='replace').splitlines() if written else [])
               if '=' in line)
    shutil.rmtree(drive, ignore_errors=True)
    checks = {}
    for key, value in EXPECTED.items():
        label = f'{key}: {got.get(key)} (Windows: {value})' + (f', {WHAT[key]}' if key in WHAT else '')
        checks[label] = got.get(key) == value
    checks[f'without a shell hook the form with -1 opens no menu: {got.get("menus")} menu'] = got.get('menus') == '1'
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'popup menu foreground host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
