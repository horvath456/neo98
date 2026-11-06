#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The focus rectangle of the native buttons in the host.

Notepad starts on the pristine C: with the original COMDLG32 and opens
Search, Find. The keyboard walks through the dialog: "Find what" gets text,
ALT+C focuses "Match case", TAB the checked direction radio button, TAB
"Find Next", TAB "Cancel". After every step a screenshot is searched for the
dotted rectangle of DrawFocusRect (every second point of a line inverted,
black on the grey face, USER): a check box and a radio button frame
their caption, a push button its content inset by the edge once
more. With the focus in the edit there is none. ESC closes Find and
Notepad is active again; then in Save As, ALT+T focuses the drop-down list "Save as type": its selection field
is filled with COLOR_HIGHLIGHT and framed.

    build/analysis-venv/bin/python tools/test_button_focus_host.py [--no-build]

Results in build/button-focus-host/results.json, screenshots beside it.
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

FACE, BLACK, HIGHLIGHT = 0xc0c0c0, 0x000000, 0x000080
# Notepad and its dialogs stand at the same place in every run. The focus
# rectangles in screen pixels, right and bottom exclusive:
EXPECTED = {
    # "Save as type" (CBS_DROPDOWNLIST): the item rectangle, with
    # the item at (3,3) and tmHeight+2 high as combo_contract measures it on
    # Win98 SE.
    'combo': [[156, 334, 367, 349]],
    # The edit "Find what" has the caret, no rectangle.
    'edit': [],
    # "Match case": the caption at 140,310 (13 + 3 + 2 points right of the
    # control at 122), one border before, 13 + 1 + 2 points high.
    'check': [[139, 309, 197, 325]],
    # "Find Next" and then "Cancel", each the default push button while it
    # has the focus (389..464 x 247..270, 276..299): inset by edge, border
    # and border.
    'next': [[393, 251, 460, 266]],
    'cancel': [[393, 280, 460, 295]],
    # "Down" after ALT+D: the caption at 341 is 28 points wide (D 8, o 6,
    # w 8, n 6 in SSERIFE.FON), so the right side of the frame lies at 369,
    # next to the last column of the n. The control itself reaches to 376.
    'radio': [[340, 303, 370, 319]],
    # The dialog is gone.
    'after': [],
}
HIGHLIGHT_PIXEL = (364, 340)


def alt(step, vk):
    return [f'{step}:keydown=a4', f'{step}:key={vk}', f'{step}:keyup=a4']


def image(path):
    """A P6 PPM as width, height and a function of x and y."""
    data = path.read_bytes()
    fields, at = [], 0
    while len(fields) < 4:
        while data[at:at + 1].isspace():
            at += 1
        if data[at:at + 1] == b'#':
            at = data.index(b'\n', at) + 1
            continue
        end = at
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[at:end])
        at = end
    width, height = int(fields[1]), int(fields[2])
    at += 1

    def pixel(x, y):
        o = at + (y * width + x) * 3
        return (data[o] << 16) | (data[o + 1] << 8) | data[o + 2]
    return width, height, pixel


def dotted(path, minimum=8, bounds=None):
    """Every rectangle DrawFocusRect left in a screenshot, as [left, top,
    right, bottom] with the right and bottom edge exclusive. A line of it
    alternates between two colours point by point (black on the grey face,
    black and a light colour on the highlight); the four sides must be such
    lines."""
    width, height, pixel = image(path)
    x0, y0, x1, y1 = bounds or (0, 0, width, height)

    def alternating(points):
        values = [pixel(x, y) for x, y in points]
        return (len(values) >= 3 and values[0] != values[1]
                and all(v == values[i % 2] for i, v in enumerate(values)))
    runs = []
    for y in range(y0, y1):
        x = x0
        while x < x1 - 2:
            end = x + 1
            if pixel(x, y) != pixel(x + 1, y):
                while end + 1 < x1 and pixel(end + 1, y) == pixel(end - 1, y) and pixel(end + 1, y) != pixel(end, y):
                    end += 1
            if end - x + 1 >= minimum and BLACK in (pixel(x, y), pixel(x + 1, y)):
                runs.append((x, end + 1, y))
                x = end
            else:
                x += 1
    found = []
    for left, right, top in runs:
        for other_left, other_right, bottom in runs:
            if bottom - top < 4 or abs(other_left - left) > 1 or abs(other_right - right) > 1:
                continue
            # A line may start or end with a point that has the colour
            # around it; the side is the column that alternates.
            l, r = min(left, other_left), max(right, other_right)
            if not alternating([(l, y) for y in range(top, bottom + 1)]):
                l += 1
            if not alternating([(r - 1, y) for y in range(top, bottom + 1)]):
                r -= 1
            if alternating([(l, y) for y in range(top, bottom + 1)]) and \
                    alternating([(r - 1, y) for y in range(top, bottom + 1)]):
                box = [l, top, r, bottom + 1]
                if box not in found:
                    found.append(box)
    return found


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
    out = neo98.output_directory('button-focus-host')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    names = ('combo', 'edit', 'check', 'next', 'cancel', 'radio', 'after')
    shots = {name: out / f'{name}.ppm' for name in names}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=Notepad', '0:idle=1500',
             # Search, Find from the keyboard.
             *alt(1, '53'), '1:wait=#32768', '1:idle=500', '1:type=f',
             '1:wait=#32770|Find', '1:idle=1500', '2:type=abc', '2:idle=800', f'2:shot={shots["edit"]}',
             *alt(3, '43'), '3:idle=800', f'3:shot={shots["check"]}',
             '4:key=09', '4:idle=800', '4.5:key=09', '4.5:idle=800', f'4.5:shot={shots["next"]}',
             '5:key=09', '5:idle=800', f'5:shot={shots["cancel"]}',
             *alt(6, '44'), '6:idle=800', f'6:shot={shots["radio"]}',
             '7:key=1b', '7:idle=1000', f'7:shot={shots["after"]}',
             # ESC gave Notepad the activation back: File, Save As,
             # and ALT+T focuses the drop-down list "Save as type".
             *alt(8, '46'), '8:wait=#32768', '8:idle=500', '8:type=a',
             '8:wait=#32770|Save As', '8:idle=1500', *alt(9, '54'), '9:idle=800', f'9:shot={shots["combo"]}',
             '10:key=1b', '10:idle=1000', '11:menu=quit']
    code, text, log_path = run(out, steps, drive_c, registry, binary)
    shutil.rmtree(drive_c, ignore_errors=True)
    # The desktop wallpaper also contains alternating black/blue pixels.
    # Inspect the whole Notepad window, including every tested dialog, so
    # wallpaper rectangles do not masquerade as control focus frames.
    boxes = {name: dotted(shot, bounds=(64, 55, 736, 535)) if shot.exists() else None
             for name, shot in shots.items()}
    highlight = image(shots['combo'])[2](*HIGHLIGHT_PIXEL) if shots['combo'].exists() else None
    checks = {
        'Notepad open': 'input wait=Notepad reached' in text,
        'Save As and Find opened': 'input wait=#32770|Save As reached' in text
                                   and 'input wait=#32770|Find reached' in text,
        'screenshots written': all(shot.exists() for shot in shots.values()),
        'the selection field of "Save as type" is highlighted': highlight == HIGHLIGHT,
        'hosts ended': code == 0,
    }
    for name, expected in EXPECTED.items():
        checks[f'{name}: focus rectangles {expected}'] = boxes.get(name) == expected
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks, 'boxes': boxes,
                                                 'highlight': highlight},
                                                indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'boxes: {json.dumps(boxes)} highlight: {highlight}')
        print(f'BUTTON FOCUS HOST FAIL: see {log_path}')
        return 1
    print(f'BUTTON FOCUS HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
