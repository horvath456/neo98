#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The labels of the icons on the desktop and in My Computer, on neo98-kms.

The list view of COMCTL32 measures a label with DrawText and wraps a long
one to two lines; one too long for two lines ends with "...". This test
keeps the labels of Windows 98 the way Windows 98 shows them while DrawText
changes (the word ellipsis, DT_WORD_ELLIPSIS, is
tools/test_draw_text_ellipsis_host.py). Over a copy of setup's drive C:
Explorer starts as the shell and My Computer opens in its large icons:

  desktop      "Internet Explorer", "Setup MSN Internet A...", "Outlook
               Express" and "Connect to the Internet" take two lines each,
               "Recycle Bin" one
  my computer  "Dial-Up Networking" and "Scheduled Tasks" take two lines,
               "Printers" one, and the focus rectangle of drive C: has dotted
               edges above and below its name

    build/analysis-venv/bin/python tools/test_icon_labels_host.py [--no-build] [--kms KMS]
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
WORK = ROOT / 'build/icon-labels'
WHITE, BLACK = b'\xff\xff\xff', b'\0\0\0'
# The bands of the labels below the icons of the desktop of 1024x768 (white
# text), and of the icons of My Computer at the top left of the screen
# (black text), with the lines each takes.
DESKTOP = {'Internet Explorer': ((0, 186, 76, 218), 2), 'Recycle Bin': ((0, 261, 76, 280), 1),
           'Setup MSN Internet A...': ((0, 336, 76, 366), 2), 'Outlook Express': ((0, 486, 76, 518), 2),
           'Connect to the Internet': ((0, 561, 76, 592), 2)}
MY_COMPUTER = {'Printers': ((350, 212, 396, 245), 1), 'Dial-Up Networking': ((490, 212, 556, 245), 2),
               'Scheduled Tasks': ((568, 212, 630, 245), 2)}
# A point of the background of the label "Connect to the Internet", the
# last icon the desktop shows.
CONNECT = (10, 575)
# The focus rectangle around "Neo98 (C:)", the first icon of My Computer.
DRIVE_C = (272, 320, 213, 227)


def pixel(picture, x, y):
    width, _, data = picture
    i = (y * width + x) * 3
    return bytes(data[i:i + 3])


def lines(picture, box, colour):
    """The runs of rows of a band that hold a pixel of the colour."""
    left, top, right, bottom = box
    count, inside = 0, False
    for y in range(top, bottom):
        hit = any(pixel(picture, x, y) == colour for x in range(left, right))
        if hit and not inside:
            count += 1
        inside = hit
    return count


def dotted(picture, left, right, y):
    """Whether row y is black on every other pixel and not black between."""
    black = [pixel(picture, x, y) == BLACK for x in range(left, right)]
    return any(all(black[phase::2]) and not any(black[1 - phase::2]) for phase in (0, 1))


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
    desktop, folder = WORK / 'desktop.png', WORK / 'my-computer.png'
    # Each picture once its window is complete and the machine quiet, not
    # at a time: the desktop adds Connect to the Internet, its last icon,
    # after a pause, so the first picture also waits for the teal of its
    # label.
    steps = ['0:wait=Shell_TrayWnd', f'0:wait={CONNECT[0]},{CONNECT[1]},008080', '0:idle=500', f'0:shot={desktop}',
             '0.5:dblclick=30,20', '0.5:wait=CabinetWClass', '0.5:idle=500', f'0.5:shot={folder}', '1:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\WINDOWS\\EXPLORER.EXE', '--time-limit', '240'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=300)
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(result.stdout + result.stderr)
    text = result.stdout + result.stderr
    timed_out = [line for line in text.splitlines() if 'timed out after' in line]
    checks = {f'the shell, My Computer and a quiet machine came: {timed_out}': not timed_out}
    pictures = [read_png(shot) if shot.is_file() else None for shot in (desktop, folder)]
    checks['the desktop and My Computer show'] = all(pictures)
    if all(pictures):
        for name, (box, expected) in DESKTOP.items():
            found = lines(pictures[0], box, WHITE)
            checks[f'desktop: "{name}" takes {expected} line(s): {found}'] = found == expected
        for name, (box, expected) in MY_COMPUTER.items():
            found = lines(pictures[1], box, BLACK)
            checks[f'my computer: "{name}" takes {expected} line(s): {found}'] = found == expected
        left, right, top, bottom = DRIVE_C
        checks['my computer: the focus rectangle of drive C: is dotted above and below its name'] = (
            dotted(pictures[1], left, right, top) and dotted(pictures[1], left, right, bottom)
            and lines(pictures[1], (left, top + 1, right, bottom), BLACK) == 1)
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Pictures and log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'icon labels: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
