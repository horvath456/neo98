#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""MenuHelp of the 16-bit COMMCTRL with File Manager of Windows 98.

WINFILE.EXE hands every WM_MENUSELECT to MenuHelp (COMMCTRL.2), which shows
the help line of the selected item in the simple part of the status bar. The
test opens the Options menu with the mouse on a desktop of 1024x768 with the
picture in memory, over a private copy of drive C:

  popup    the Options popup: its line is string F90h + 4 of WINFILE, found
           by the popup's menu, which the 16-bit WM_MENUSELECT carries in
           wParam where the 32-bit one has the position
  item     the Toolbar item: the line of its command
  closed   Escape closes the menu; the status bar shows what it showed before

Before MenuHelp was ported the first click on the menu bar stopped the run.

    build/analysis-venv/bin/python tools/test_win16_menu_help_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/win16-menu-help'
# The text of the status bar of File Manager's first window on 1024x768.
STATUS = (4, 744, 420, 764)


def area(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return [bytes(data[(y * width + left) * 3:(y * width + right) * 3]) for y in range(top, bottom)]


def text_columns(picture, box):
    """The columns of the box that hold a black pixel, the text of the line."""
    width, _, data = picture
    left, top, right, bottom = box
    return {x for x in range(left, right) for y in range(top, bottom)
            if bytes(data[(y * width + x) * 3:(y * width + x) * 3 + 3]) == b'\0\0\0'}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
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
    # Options stands at 160,31 in the menu bar; Toolbar is the fourth item of
    # its popup, below the separator.
    steps = ['0:wait=WFS_Frame', '4:shot={out}/before.png', '5:click=160,31', '7:shot={out}/popup.png',
             '8:move=180,113', '8.5:move=181,114', '10:shot={out}/item.png', '11:key=1b', '11.5:key=1b',
             '13:shot={out}/closed.png', '14:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\WINFILE.EXE', '--time-limit', '40'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    text = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(text)
    stops = [line for line in text.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None
                for name in ('before', 'popup', 'item', 'closed')}
    if any(picture is None for picture in pictures.values()):
        status = {}
    else:
        status = {name: area(picture, STATUS) for name, picture in pictures.items()}
    lines = {name: text_columns(picture, STATUS) for name, picture in pictures.items() if picture is not None}
    popup, item = lines.get('popup', set()), lines.get('item', set())
    checks = {
        'popup: opening Options with the mouse runs on (no stop at COMMCTRL.2)': not stops and bool(status),
        'popup: the status bar shows a help line for the popup':
            bool(status) and status['popup'] != status['before'] and len(popup) > 60,
        'item: the help line of the Toolbar item replaces it':
            bool(status) and status['item'] != status['popup'] and len(item) > 60,
        'closed: closing the menu brings back the status bar of before':
            bool(status) and status['closed'] == status['before'],
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'win16 menu help: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
