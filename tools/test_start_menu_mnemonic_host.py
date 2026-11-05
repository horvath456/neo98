#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The underlined letter of a Start menu item runs the item.

Explorer starts on the pristine C:, a click opens the Start menu, and the key
R is typed. The Start menu is the menu band of the original shell (a toolbar
in a BaseBar); its WH_GETMESSAGE hook hands the tray's WM_KEYDOWN to the
toolbar before Explorer calls TranslateMessage. USER posts WM_CHAR to
the window the MSG names, so the toolbar gets the 'r', finds "&Run..." and
runs it. The check waits for the Run dialog.

    build/analysis-venv/bin/python tools/test_start_menu_mnemonic_host.py [--no-build]

Results in build/start-menu-mnemonic-host/results.json.
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


def main():
    out = neo98.output_directory('start-menu-mnemonic-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    menu_shot, run_shot = out / 'menu.ppm', out / 'run.ppm'
    for shot in (menu_shot, run_shot):
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # The Start button is at 25,587. The menu band's window is a BaseBar.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=2000', '1:click=25,587', '1:wait=BaseBar', '1:idle=2000',
             f'2:shot={menu_shot}', '3:type=r', '3:wait=#32770|Run', '3:idle=1000',
             f'4:shot={run_shot}', '5:menu=quit']
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
                                stdin=subprocess.DEVNULL, timeout=900)
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    checks = {
        'Start menu open': 'input wait=BaseBar reached' in text,
        'R opens Run': 'input wait=#32770|Run reached' in text,
        'screenshots written': menu_shot.exists() and run_shot.exists(),
        'host ended': result.returncode == 0,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'START MENU MNEMONIC FAIL: see {log_path}')
        return 1
    print(f'START MENU MNEMONIC PASS: {run_shot}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
