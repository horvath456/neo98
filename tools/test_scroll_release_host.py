#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A released scroll bar of a control shows nothing pressed any more.

The dialog "Open With" of SHELL32 (rundll32 shell32.dll,OpenAs_RunDLL) lists
the programs in a SysListView32 of COMCTL32 with a vertical bar of USER. The
list is a child without WS_CLIPSIBLINGS, so USER's frame of it is drawn again
only when its state changes. The inverted page region and the pushed arrow of
the bar tracker were not part of that state: after the release the region
stayed black and the arrow stayed pushed.

The host runs under SDL's dummy video driver with NEO98_HOST_INPUT. Checks:

- a click on the bar below the thumb scrolls and leaves no inverted pixel,
- a held arrow is drawn pushed, and after the release as before the press,
- the wheel scrolls the list once the list has the focus (WM_MOUSEWHEEL goes
  to the focus window, USER), and leaves it alone while the focus is
  in the description field, as Win98 does.

    build/analysis-venv/bin/python tools/test_scroll_release_host.py [--no-build]

Results in build/scroll-release-host/results.json.
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

# The dialog's owner is a plain WS_OVERLAPPED window, which Win98 gives a
# caption and frame: the dialog, placed against the owner's client area,
# stands 3 right and 20 down of where the older layout put it. The list is at
# 81,221 to 377,348 with a sunken edge of two pixels; its bar is the 16
# pixels inside the right edge, an arrow at each end.
DX, DY = 3, 20
def shifted(*points):
    return tuple(v + (DX if i % 2 == 0 else DY) for i, v in enumerate(points))
LIST = shifted(80, 203, 356, 326)
BAR = shifted(356, 203, 372, 326)
TRACK = shifted(356, 219, 372, 310)
DOWN = shifted(356, 310, 372, 326)
# The pointer covers what is right of and below its hot spot. It rests on the
# last pixels of the arrow while the arrow is held, and the comparison leaves
# those out.
DOWN_SEEN = shifted(357, 311, 369, 323)
HOLD = shifted(370, 324)
PAGE = shifted(364, 290)
ITEM = shifted(150, 280)
AWAY = (700, 540)


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def differing(first, second, box):
    (a, width), (b, _) = first, second
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if a[(y * width + x) * 3:(y * width + x) * 3 + 3] != b[(y * width + x) * 3:(y * width + x) * 3 + 3])


def count(shot, box, colours):
    body, width = shot
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if body[(y * width + x) * 3:(y * width + x) * 3 + 3] in colours)


def main():
    out = neo98.output_directory('scroll-release-host')
    checks = {}
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    # A file type nobody registered: the dialog then has its description field,
    # which takes the focus first.
    (drive_c / 'NEO98TST.ZZQ').write_bytes(b'NEO98\r\n')
    names = ('start', 'wheel-field', 'page', 'held', 'released', 'focus', 'wheel-list')
    shots = {name: out / f'{name}.ppm' for name in names}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    at = lambda p: f'{p[0]},{p[1]}'
    rest = [f'move={at(AWAY)}', 'idle=700']
    steps = ['0:wait=#32770|Open With', '0:idle=2500', f'0.5:move={at(AWAY)}', '0.5:idle=500',
             f'1:shot={shots["start"]}',
             # The focus is in the description field: the wheel is not the list's.
             '2:wheel=-3', '2:idle=700', f'2.5:shot={shots["wheel-field"]}',
             f'3:click={at(PAGE)}', '3:idle=700', *(f'3.5:{s}' for s in rest), f'4:shot={shots["page"]}',
             f'5:move={at(HOLD)}', f'5:down={at(HOLD)}', '5:idle=300', f'5.5:shot={shots["held"]}',
             f'6:up={at(HOLD)}', '6:idle=700', *(f'6.5:{s}' for s in rest), f'7:shot={shots["released"]}',
             f'8:click={at(ITEM)}', '8:idle=700', *(f'8.5:{s}' for s in rest), f'9:shot={shots["focus"]}',
             # The list stands near its end by now, so the wheel turns back.
             '10:wheel=3', '10:idle=700', f'10.5:shot={shots["wheel-list"]}',
             '11:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
               '--command', 'rundll32.exe shell32.dll,OpenAs_RunDLL C:\\NEO98TST.ZZQ', '--quiet',
               '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    checks['host ended'] = result.returncode == 0
    checks['all screenshots'] = all(shot.exists() for shot in shots.values())
    numbers = {}
    if checks['all screenshots']:
        shot = {name: pixels(shots[name]) for name in names}
        # The track is USER's dither of COLOR_BTNFACE and COLOR_BTNHIGHLIGHT;
        # DSTINVERT turns it into 3f3f3f and black.
        inverted = (b'\x3f\x3f\x3f',)
        face = (b'\xc0\xc0\xc0',)
        numbers['track, dither pixels at the start'] = count(shot['start'], TRACK, face)
        checks['the list has a bar'] = numbers['track, dither pixels at the start'] > 100
        numbers['wheel in the field, changed list pixels'] = differing(shot['start'], shot['wheel-field'], LIST)
        checks['the wheel leaves the list alone while the field has the focus'] = \
            numbers['wheel in the field, changed list pixels'] == 0
        numbers['page click, changed list pixels'] = differing(shot['start'], shot['page'], LIST)
        numbers['page click, changed bar pixels'] = differing(shot['start'], shot['page'], TRACK)
        numbers['page click, inverted pixels left'] = count(shot['page'], BAR, inverted)
        checks['a click below the thumb scrolls the list'] = (
            numbers['page click, changed list pixels'] > 200 and numbers['page click, changed bar pixels'] > 0)
        checks['the released page region is not inverted'] = numbers['page click, inverted pixels left'] == 0
        numbers['held arrow, changed pixels'] = differing(shot['page'], shot['held'], DOWN_SEEN)
        numbers['released arrow, changed pixels'] = differing(shot['page'], shot['released'], DOWN)
        checks['a held arrow is drawn pushed'] = numbers['held arrow, changed pixels'] > 10
        checks['a released arrow is drawn as before'] = numbers['released arrow, changed pixels'] == 0
        numbers['wheel in the list, changed list pixels'] = differing(shot['focus'], shot['wheel-list'], LIST)
        numbers['wheel in the list, changed bar pixels'] = differing(shot['focus'], shot['wheel-list'], TRACK)
        checks['the wheel scrolls the list that has the focus'] = (
            numbers['wheel in the list, changed list pixels'] > 200
            and numbers['wheel in the list, changed bar pixels'] > 0)
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': result.returncode,
                                                  'steps': steps, 'numbers': numbers, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(json.dumps(numbers))
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'SCROLL RELEASE FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    shutil.rmtree(drive_c, ignore_errors=True)
    print(f'SCROLL RELEASE PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
