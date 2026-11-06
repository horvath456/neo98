#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Notepad's text stays while a menu or Save As is open.

Notepad starts on the pristine C: and gets a line long enough to reach past
the Save As dialog. The menu File and the dialog both stand over the start of
the text: the popup drops below File at the left edge of the client area, and
the Save As template of COMDLG32 (NEWFILEOPENORD, 1547/1548) has position 0,0
without DS_CENTER, so the dialog opens at the owner's client origin as in
Win98. What they cover is hidden; a short input sample used only "abc",
which lies entirely under both. The test checks

- that the part of the line right of the popup and right of the dialog keeps
  its pixels while each is open,
- that the part under them comes back unchanged after each closes.

    build/analysis-venv/bin/python tools/test_notepad_text_behind_host.py [--no-build]

Results in build/notepad-text-behind-host/results.json, screenshots beside it.
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
from test_mnemonic_host import alt, pixels  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

LINE = 'abcdefghijklmnopqrstuvwxyz0123456789 hello world line and more text to the right'
BLACK = 0x000000
# Notepad's window stands at the same place in every run. The first text line
# in screen pixels; an empty line keeps the caret below this sampled band.
ROW = range(103, 118)
UNDER_MENU = range(72, 180)       # covered by the popup File
RIGHT_OF_MENU = range(190, 480)
UNDER_DIALOG = range(72, 490)     # covered by Save As
RIGHT_OF_DIALOG = range(500, 700)


def region(shot, xs):
    pixel = pixels(shot)
    return [pixel(x, y) for y in ROW for x in xs]


def run(out, steps, drive_c, registry, binary):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\NOTEPAD.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host-notepad.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    return result.returncode, log_path.read_text(errors='replace'), log_path


def main():
    out = neo98.output_directory('notepad-text-behind-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    names = ('before', 'menu', 'after-menu', 'save-as', 'after-save-as')
    shots = {name: out / f'{name}.ppm' for name in names}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Notepad', '0:idle=1500', f'1:type={LINE}', '1:key=0d', '1:key=0d', '1:type=second line',
             '1:idle=800', f'1:shot={shots["before"]}',
             *alt(2, '46'), '2:wait=#32768', '2:idle=800', f'2:shot={shots["menu"]}',
             '2:key=1b', '2:key=1b', '2:idle=800', f'2:shot={shots["after-menu"]}',
             *alt(3, '46'), '3:wait=#32768', '3:idle=500', '3:type=a', '3:wait=#32770|Save As',
             '3:idle=1500', f'3:shot={shots["save-as"]}', '3:key=1b', '3:idle=1500',
             f'3:shot={shots["after-save-as"]}', '4:menu=quit']
    code, text, log_path = run(out, steps, drive_c, registry, binary)
    shutil.rmtree(drive_c, ignore_errors=True)
    have = all(shot.exists() for shot in shots.values())
    checks = {'Notepad open': 'input wait=Notepad reached' in text,
              'the menu File opened': 'input wait=#32768 reached' in text,
              'Save As opened': 'input wait=#32770|Save As reached' in text,
              'screenshots written': have}
    if have:
        before = {name: region(shots['before'], xs) for name, xs in
                  (('under menu', UNDER_MENU), ('right of menu', RIGHT_OF_MENU),
                   ('under dialog', UNDER_DIALOG), ('right of dialog', RIGHT_OF_DIALOG))}
        checks.update({
            'the typed line is on screen': all(BLACK in part for part in before.values()),
            'text right of the open menu stays': region(shots['menu'], RIGHT_OF_MENU) == before['right of menu'],
            'text under the menu comes back': region(shots['after-menu'], UNDER_MENU) == before['under menu'],
            'text right of Save As stays': region(shots['save-as'], RIGHT_OF_DIALOG) == before['right of dialog'],
            'text under Save As comes back':
                region(shots['after-save-as'], UNDER_DIALOG) == before['under dialog'],
        })
    checks['host ended'] = code == 0
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'NOTEPAD TEXT BEHIND HOST FAIL: see {log_path}')
        return 1
    print(f'NOTEPAD TEXT BEHIND HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
