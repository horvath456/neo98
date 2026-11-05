#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The menu bar opens from the keyboard.

Notepad starts on the pristine C: in the host. The script types text with an
AltGr character and presses AltGr alone, which must not open a menu. Then it
opens the menu bar the three ways Win98 offers: ALT alone and the arrows (RIGHT
to Edit, DOWN drops it), F10 and ENTER (drops File), and ALT held with F, where
the mnemonic A of File runs Save As. Each popup is waited for as window class
#32768, and ESC closes it; the letters typed between the steps must reach the
text, so no menu mode was left behind. The saved file proves both.

    build/analysis-venv/bin/python tools/test_key_menu_host.py [--no-build]

Results in build/key-menu-host/results.json, screenshots beside it.
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

EXPECTED = b'a@bcde'


def main():
    out = neo98.output_directory('key-menu-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    shots = {name: out / f'{name}.ppm' for name in ('bar', 'edit', 'file', 'save')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Notepad', '0:idle=1500',
             # AltGr+Q is @ on the German layout; AltGr alone opens nothing.
             '1:type=a@b', '1:keydown=a2', '1:keydown=a5', '1:keyup=a5', '1:keyup=a2', '1:type=c',
             '1:idle=800',
             # ALT alone selects File in the bar; RIGHT moves to Edit, DOWN drops it.
             '2:key=a4', '2:idle=800', f'2:shot={shots["bar"]}', '2:key=27', '2:key=28',
             '2:wait=#32768', '2:idle=800', f'2:shot={shots["edit"]}', '2:key=1b', '2:key=1b',
             '2:idle=800', '2:type=d', '2:idle=800',
             # F10 selects File, ENTER drops it.
             '3:key=79', '3:key=0d', '3:wait=#32768', '3:idle=800', f'3:shot={shots["file"]}',
             '3:key=1b', '3:key=1b', '3:idle=800', '3:type=e', '3:idle=800',
             # ALT+F drops File, A runs Save As.
             '4:keydown=a4', '4:key=46', '4:keyup=a4', '4:wait=#32768', '4:idle=800',
             f'4:shot={shots["save"]}', '4:type=a',
             '4:wait=#32770|Save As', '4:idle=1000', '5:type=C:\\B2670.TXT', '5:key=0d',
             '5:idle=3000', '6:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    saved = next((p for p in drive_c.iterdir() if p.name.upper() == 'B2670.TXT'), None)
    content = saved.read_bytes() if saved else b''
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    popups = text.count('input wait=#32768 reached')
    checks = {
        'Notepad open': 'input wait=Notepad reached' in text,
        'three popups dropped from the keyboard': popups == 3,
        'Save As opened by the mnemonic': 'input wait=#32770|Save As reached' in text,
        'file saved': saved is not None,
        'typing reached the text between the menus': content == EXPECTED,
        'screenshots written': all(shot.exists() for shot in shots.values()),
        'host ended': result.returncode == 0,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks,
                                                 'content': content.decode('cp1252', 'replace')},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'saved: {content!r}\nwanted: {EXPECTED!r}')
        print(f'KEY MENU HOST FAIL: see {log_path}')
        return 1
    print(f'KEY MENU HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
