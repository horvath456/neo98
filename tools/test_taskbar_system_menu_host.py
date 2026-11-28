#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The system menu of 0313h from the keyboard, beside the taskbar's button.

The taskbar sends a window 0313h with lParam -1 when the menu of its button
is asked for from the keyboard. USER's DefWindowProc then asks the shell
hook for the button's rectangle (HSHELL_GETMINRECT; SHELL sends its
windows the registered SHELLHOOK message with code 5 and the taskbar fills
in its button) and tracks the system menu at the rectangle's top left
corner with TPM_VERTICAL and the rectangle excluded: the menu opens above
the button, its left edge on the button's. With no shell hook it opens
nothing. Neo98 opened nothing in both cases: the hook's send to the taskbar,
in Explorer's process, became a post and no rectangle came back.

tools/taskbar_menu_fixture.c sends itself the message, once started by
Explorer and once alone.

    build/analysis-venv/bin/python tools/test_taskbar_system_menu_host.py [--no-build]
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

from ui_clipboard_test_support import ROOT, serial, machine, host  # noqa: E402
import neo98  # noqa: E402
from native_app import build_host  # noqa: E402
import win98_c  # noqa: E402


def compile_fixture(target):
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/taskbar_menu_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-o', str(target)],
                   env=neo98.tool_environment(), check=True)


def parse(lines):
    found = {}
    for line in lines:
        words = line.split()
        if words and words[0] in ('menu', 'band', 'tray'):
            found[words[0]] = tuple(int(w) for w in words[1:5])
        elif words:
            found.setdefault(words[0], words[1:])
    return found


def with_explorer(out):
    with machine(out) as drive:
        compile_fixture(drive / 'TASKMENU.EXE')
        host(out, drive, r'C:\WINDOWS\EXPLORER.EXE',
             steps=f'4:key=1b;8:key=1b;10:wait=Shell_TrayWnd;11:open={drive}/TASKMENU.EXE;'
                   f'11.2:wait=TaskMenu;14.5:shot={out}/menu.ppm', seconds=20,
             # Explorer's first start on the pristine drive runs the setup of
             # the first logon before the taskbar is there.
             deadline=240)
        written = win98_c.path('C:\\TASKMENU.TXT', drive)
        return written.read_text().splitlines() if written else []


def alone(out):
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(win98_c.use_pristine().resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    compile_fixture(drive / 'TASKMENU.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='30',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\TASKMENU.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\TASKMENU.TXT', drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    return lines


def main():
    if '--no-build' not in sys.argv:
        build_host()
    out = neo98.output_directory('taskbar-system-menu')
    shutil.rmtree(out, ignore_errors=True)
    with serial():
        shell_lines = with_explorer(out / 'explorer')
        alone_lines = alone(out / 'alone')
    shell, single = parse(shell_lines), parse(alone_lines)
    menu, band, tray = shell.get('menu'), shell.get('band'), shell.get('tray')
    checks = {
        'with Explorer the program sent 0313h with -1 and wrote what it saw': 'sent' in shell and 'done' in shell,
        f'the system menu opened: {shell.get("enter")}': 'enter' in shell,
        f'its default item is Maximize: {shell.get("popup")}': shell.get('popup') == ['default=f030'],
        f'the taskbar and its band of buttons are there: {tray} {band}':
            bool(tray and band and band[2] > band[0] and tray[1] <= band[1]),
    }
    if menu and band and menu[2] > menu[0]:
        # The program's button is the first of the band: at its left edge,
        # within the few pixels of the band's margin.
        checks[f'the menu stands on the button: menu {menu}, band {band}'] = \
            band[0] <= menu[0] <= band[0] + 4 and band[1] <= menu[3] <= band[1] + 4
        checks['and above the taskbar, not over the button'] = menu[1] < menu[3] <= band[1] + 4
    else:
        checks[f'the menu has a window with a rectangle: {menu}'] = False
    checks.update({
        'alone the program sent 0313h with -1 and closed': 'sent' in single and 'done' in single,
        'without a shell hook no menu opened': 'enter' not in single and 'menu' not in single,
    })
    (out / 'results.json').write_text(json.dumps({'explorer': shell_lines, 'alone': alone_lines,
                                                  'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    logs and picture in', out)
    print(f'taskbar system menu host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
