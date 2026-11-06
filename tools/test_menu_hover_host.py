#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A submenu of a menu bar opens when the pointer rests on its item.

My Computer opens; a click on View opens its menu, and the pointer comes to
rest on "Explorer Bar". USER sets a system timer on the popup window for the
delay of SPI_GETMENUSHOWDELAY and opens the submenu when it fires, without a
click (src/x86_menu_tracking.cpp). The check reads the gray face of the
submenu to the right of the View menu.

    build/analysis-venv/bin/python tools/test_menu_hover_host.py [--no-build]

Results in build/menu-hover-host/results.json.
"""
from __future__ import annotations

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
win98_c.use_pristine()  # tests start from setup's C:, never the user's


def face_pixels(path):
    """Menu-gray pixels where the Explorer Bar submenu opens (x 280..420, y 180..220)."""
    data = Path(path).read_bytes()
    magic, size, depth, body = data.split(b'\n', 3)
    width = int(size.split()[0])
    return sum(body[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\xc0\xc0\xc0'
               for y in range(180, 220) for x in range(280, 420))


def main():
    out = neo98.output_directory('menu-hover-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    shot = out / 'hover.ppm'
    shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # View in the menu bar at 163,90; "Explorer Bar" at 190,147. No click on
    # the item: the pointer only rests there for two seconds.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=2000', '1:dblclick=37,20', '1:wait=CabinetWClass',
             '1:idle=3000', '2:click=163,90', '2:idle=1000', '3:move=190,147', '3.5:move=191,148',
             f'5.5:shot={shot}', '6:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    shutil.rmtree(drive_c, ignore_errors=True)
    checks = {'screenshot written': shot.exists()}
    face = face_pixels(shot) if shot.exists() else 0
    checks['submenu open without a click'] = face > 3000
    checks['host ended'] = result.returncode == 0
    (out / 'results.json').write_text(json.dumps({'face_pixels': face, 'steps': steps,
                                                  'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'MENU HOVER FAIL: face={face}; see {log_path}')
        return 1
    print(f'MENU HOVER PASS: face={face}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
