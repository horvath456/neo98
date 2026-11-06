#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A closed combo box list leaves nothing behind.

Display Properties, page Settings: the list of "Colors" opens over a group
box, a static field and a check box of a page with WS_CLIPCHILDREN. The group
box paints only its frame and the page does not paint below its children, so
nothing below the list can bring those pixels back by painting. Win98 does
not need them to: the list is a window of the class ComboLBox, which has
CS_SAVEBITS, and USER puts the saved pixels back when the list closes.

The host runs under SDL's dummy video driver with NEO98_HOST_INPUT: the page,
a screenshot, the list open, a screenshot, the list closed, a screenshot.
Checks:

- the open list covers the pixels below the field,
- after closing, every pixel of the dialog is what it was before.

    build/analysis-venv/bin/python tools/test_combo_savebits_host.py [--no-build]

Results in build/combo-savebits-host/results.json.
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
import arm64_run  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

# The button of the drop-down field, and a place for the pointer away from
# the dialog while the screenshots are taken.
ARROW = (240, 382)
AWAY = (700, 540)
# The list opens below the field: 97,391 to 252,445.
# The selection field is tmHeight+2 high with its edit at (3,3), as the
# combo_contract geometry measured on Win98 SE; the list starts 3 px below
# the older NEO98 layout.
LIST = (98, 395, 251, 447)
DIALOG = (64, 55, 468, 503)


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def differing(first, second, box):
    (a, width), (b, _) = first, second
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom)
               if a[(y * width + left) * 3:(y * width + right) * 3] != b[(y * width + left) * 3:(y * width + right) * 3]
               for x in range(left, right)
               if a[(y * width + x) * 3:(y * width + x) * 3 + 3] != b[(y * width + x) * 3:(y * width + x) * 3 + 3])


def main():
    out = neo98.output_directory('combo-savebits-host')
    checks = {}
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    shots = {name: out / f'{name}.ppm' for name in ('before', 'open', 'closed')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    away = f'{AWAY[0]},{AWAY[1]}'
    arrow = f'{ARROW[0]},{ARROW[1]}'
    steps = ['0:wait=#32770|Display Properties', '0:idle=2500', f'0.5:move={away}', '0.5:idle=500',
             f'1:shot={shots["before"]}',
             f'2:click={arrow}', '2:idle=1500', f'2.5:move={away}', '2.5:idle=500', f'3:shot={shots["open"]}',
             f'4:click={arrow}', '4:idle=1500', f'4.5:move={away}', '4.5:idle=500', f'5:shot={shots["closed"]}',
             '6:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='90')
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
               '--command', 'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,3', '--quiet',
               '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try: code = process.wait(timeout=110)
        except subprocess.TimeoutExpired:
            arm64_run.end(process)
            code = 'timeout'
    checks['host ended'] = code == 0
    checks['three screenshots'] = all(shot.exists() for shot in shots.values())
    numbers = {}
    if checks['three screenshots']:
        before, opened, closed = (pixels(shots[name]) for name in ('before', 'open', 'closed'))
        numbers['open list, changed pixels'] = differing(before, opened, LIST)
        numbers['closed list, changed pixels'] = differing(before, closed, DIALOG)
        area = (LIST[2] - LIST[0]) * (LIST[3] - LIST[1])
        checks['the open list covers what is below the field'] = numbers['open list, changed pixels'] * 2 > area
        checks['the closed list leaves nothing behind'] = numbers['closed list, changed pixels'] == 0
        body, width = opened
        def pixel(x, y):
            at = (y * width + x) * 3
            return body[at:at + 3]
        # Windows 98: one black border outside four full 13-pixel rows.
        checks['popup has one black outer border'] = all(pixel(97, y) == b'\0\0\0' for y in range(394, 448))
        checks['client margin is not overpainted by an inner bevel'] = all(
            pixel(98, y) == (b'\xff\xff\xff' if y < 434 else b'\0\0\x80')
            for y in range(395, 447))
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': code,
                                                  'steps': steps, 'numbers': numbers, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(json.dumps(numbers))
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'COMBO SAVEBITS FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'COMBO SAVEBITS PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
