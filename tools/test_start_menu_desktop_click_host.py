#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""After the Start menu, a click on the desktop closes it and selects an icon.

Explorer starts on the pristine C:. A click on Start opens the Start menu (a
BaseBar of the tray thread, which holds the mouse). A click on the empty
desktop must close it, and a click on "My Documents" must select the icon:
its label shows white on navy, as it does without the Start menu.

Two faults stood in the way:

1. One capture for all threads. The desktop's list view runs on another
   thread of Explorer and calls SetCapture in its drag check; that took the
   menu band's capture, the menu band's message hook saw GetCapture name a
   foreign window and ignored the click outside, and the menu stayed open.
   Win98 keeps the capture per input queue (queue word 18h).
2. A stale focus in the desktop's queue. When the tray took the foreground,
   the list view got WM_KILLFOCUS but stayed the focus of its queue, so its
   SetFocus on the next click returned at once without WM_SETFOCUS. DefView
   answers WM_MOUSEACTIVATE with MA_NOACTIVATE, so nothing else gave the
   list view its focus back, and it drew the selection without focus, that
   is not at all.

    build/analysis-venv/bin/python tools/test_start_menu_desktop_click_host.py [--no-build]

Results in build/start-menu-desktop-click-host/results.json.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import first_logon  # noqa: E402
from win98_folder import journal_record  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's


def read_ppm(path):
    data = Path(path).read_bytes()
    magic, size, depth, body = data.split(b'\n', 3)
    width = int(size.split()[0])
    return width, body


def count(path, box, colour):
    """Pixels of one colour in the box (x0, y0, x1, y1), ends exclusive."""
    width, body = read_ppm(path)
    x0, y0, x1, y1 = box
    return sum(body[(y * width + x) * 3:(y * width + x) * 3 + 3] == colour
               for y in range(y0, y1) for x in range(x0, x1))


NAVY, TEAL = b'\x00\x00\x80', b'\x00\x80\x80'
LABEL = (0, 113, 76, 127)      # the label "My Documents" under its icon at 36,95
MENU = (30, 300, 170, 560)     # where the open Start menu stands


def main():
    out = neo98.output_directory('start-menu-desktop-click-host')
    drive_c, registry = first_logon.clone(out / 'session')
    policy = r'HKEY_USERS\.Default\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer'
    registry.write_bytes(registry.read_bytes() + journal_record(3, policy, 'NoDriveTypeAutoRun')
                         + struct.pack('<III', 4, 4, 255))
    # The pixel checks expect the plain teal desktop, independent of the
    # wallpaper and pattern in setup's registry. Change only this clone.
    with registry.open('ab') as journal:
        for name in ('Wallpaper', 'Pattern'):
            journal.write(journal_record(3, r'HKEY_USERS\.Default\Control Panel\Desktop', name)
                          + struct.pack('<II', 1, 1) + b'\0')
    shots = {name: out / f'{name}.ppm' for name in ('menu', 'closed', 'selected')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # The Start button is at 25,587; 400,300 is empty desktop; My Documents
    # is the icon at 36,95.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=3000', '1:click=25,587', '1:wait=BaseBar', '1:idle=2000',
             f'2:shot={shots["menu"]}', '3:click=400,300', '3:idle=1500', f'4:shot={shots["closed"]}',
             '5:click=36,95', '5:idle=1500', f'6:shot={shots["selected"]}', '7:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(win98_c.FOLDER.resolve()), '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    shutil.rmtree(out / 'session', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    present = all(shot.exists() for shot in shots.values())
    menu_area = MENU[2] - MENU[0]
    menu_area *= MENU[3] - MENU[1]
    figures = {}
    if present:
        figures = {'teal_in_menu_area_open': count(shots['menu'], MENU, TEAL),
                   'teal_in_menu_area_after_click': count(shots['closed'], MENU, TEAL),
                   'navy_in_label': count(shots['selected'], LABEL, NAVY)}
    checks = {
        'Start menu open': 'input wait=BaseBar reached' in text and present and
                           figures['teal_in_menu_area_open'] < menu_area // 10,
        'click on the desktop closes the Start menu': present and
                           figures['teal_in_menu_area_after_click'] > menu_area * 9 // 10,
        'click on My Documents selects it': present and figures['navy_in_label'] > 200,
        'host ended': result.returncode == 0,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'figures': figures, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'START MENU DESKTOP CLICK FAIL: {figures}; see {log_path}')
        return 1
    print(f'START MENU DESKTOP CLICK PASS: {shots["selected"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
