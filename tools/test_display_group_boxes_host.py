#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The group boxes of Display Properties after a change of the screen area,
on neo98-kms.

The page Settings of DESK.CPL has WS_CLIPCHILDREN, and a group box never
paints its inside: there only the property sheet above the page paints,
which lacks the style. When a window's area came free again, the port gave
it to the ancestors without WS_CLIPCHILDREN up to the first one that has
it, and so never to the sheet: the inside of the groups Colors and Screen
area kept what was there before. After the display went to 800 by 600 and
back it was black.

The test takes the picture of the page, moves the slider of the screen area
to 800 by 600, applies it, answers No when Windows asks to keep it, and
takes the picture again: the two group boxes have no more black pixels than
before, those of their labels.

    build/analysis-venv/bin/python tools/test_display_group_boxes_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/display-group-boxes'
# On the page Settings of Display Properties as it opens on 1024x768: the
# slider of the screen area, the button Apply, and the groups Colors and
# Screen area.
SLIDER, APPLY = (385, 385), (420, 481)
GROUPS = {'Colors': (89, 357, 262, 422), 'Screen area': (272, 357, 445, 422)}
BLACK = b'\0\0\0'


def black(picture, box):
    """Black pixels in the box, three more on every side: Windows centres the
    sheet again after each change, so it may stand a pixel apart."""
    width, _, data = picture
    left, top, right, bottom = box
    return sum(data[(y * width + x) * 3:(y * width + x) * 3 + 3] == BLACK
               for y in range(top - 3, bottom + 3) for x in range(left - 3, right + 3))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    before, after = WORK / 'before.png', WORK / 'after.png'
    # Each step waits for a quiet machine; the question whether to keep
    # the setting is waited for by its title.
    steps = ['0:wait=#32770|Display Properties', '0:idle=1000', f'0:shot={before}',
             f'0.5:click={SLIDER[0]},{SLIDER[1]}', '0.5:idle=500', '1:key=25', '1:idle=1000',
             f'1.5:click={APPLY[0]},{APPLY[1]}', '1.5:idle=1500', '2:key=0d',
             '2:wait=#32770|Monitor Settings', '2:idle=1000', '2.5:key=0d', '2.5:idle=2000', f'2.5:shot={after}',
             '3:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\WINDOWS\\RUNDLL32.EXE', '--command',
                                 'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,3', '--time-limit', '120'],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=180)
        text = result.stdout + result.stderr
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(text)
    timed_out = [line for line in text.splitlines() if 'timed out after' in line]
    pictures = [read_png(path) if path.is_file() else None for path in (before, after)]
    changes = [line for line in text.splitlines() if 'DDRAWDRIVER display mode' in line]
    checks = {
        f'every window and quiet moment came: {timed_out}': not timed_out,
        'both pictures were taken': all(pictures),
        f'the display changed its mode and back: {len(changes)}': len(changes) >= 2,
    }
    for name, box in GROUPS.items():
        counts = [black(picture, box) for picture in pictures] if all(pictures) else [0, -1]
        # The labels are black in both; an inside left black adds thousands.
        checks[f'{name}: the inside of the group is not black after (black pixels {counts[0]} -> {counts[1]})'] = \
            0 <= counts[1] <= counts[0] + 100
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Pictures and log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'display group boxes: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
