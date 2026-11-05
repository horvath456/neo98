#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A tray icon's menu leaves nothing on the taskbar when it closes.

The context menu of a tray icon opens over the taskbar. Win98's menu code
puts a popup on top with SetWindowPos(HWND_TOPMOST); the port ordered it
like any window, below the taskbar, which is topmost itself. The part of the
menu over the taskbar was then outside its visible region, the bits saved
under it did not take it back, and its last row and border stayed on the
taskbar after a click on an item (src/user_menu_tracking.inc, PlacePopup).
The test opens the menu of the volume icon, picks "Adjust Audio Properties"
and compares the taskbar under the menu with its picture before.
On the desktop of the first logon, without a window.

    build/analysis-venv/bin/python tools/test_tray_menu_close_host.py [--no-build]
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import first_logon  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

# The volume icon at 727,587; its menu opens at 566,545 up to 727,587, and
# "Adjust Audio Properties" lies at 640,575. The taskbar under the menu from
# its top edge at 572 down to the menu's bottom.
VOLUME, ITEM = (727, 587), (640, 575)
AREA = (566, 572, 727, 587)


def picture(path):
    data = path.read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    return width, parts[3]


def area(path):
    width, pixels = picture(path)
    left, top, right, bottom = AREA
    return b''.join(pixels[(y * width + left) * 3:(y * width + right) * 3] for y in range(top, bottom))


def main():
    out = neo98.output_directory('tray-menu-close-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive, registry = first_logon.clone(out / 'session')
    # The pointer leaves the taskbar before the last picture.
    steps = ['0:wait=Shell_TrayWnd', f'5:shot={out}/before.ppm', '6:rclick=%d,%d' % VOLUME, f'8:shot={out}/menu.ppm',
             '8.5:click=%d,%d' % ITEM, '11:move=400,300', f'12:shot={out}/after.ppm', '13:tops=1']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='20', NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_TEST_CAMERA='none')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    with (out / 'host.log').open('w') as log:
        code = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--registry', str(registry),
                               '--crash-report-dir', str(out / 'reports')], cwd=ROOT, env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=240).returncode
    text = (out / 'host.log').read_text(errors='replace')
    shots = [out / name for name in ('before.ppm', 'menu.ppm', 'after.ppm')]
    have = all(p.is_file() for p in shots)
    checks = {
        'all pictures taken': have,
        'the menu stands over the taskbar': have and area(shots[1]) != area(shots[0]),
        'the taskbar under the menu is as before once it closed': have and area(shots[2]) == area(shots[0]),
        'no crash': 'NEO98 HOST failed' not in text and 'CRASHREPORT' not in text and code == 0,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass; log {out / "host.log"}')
    subprocess.run(['rm', '-rf', str(out / 'session')])
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
