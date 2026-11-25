#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Program Manager of Windows 98 on neo98-kms.

PROGMAN.EXE is a 16-bit program. Setup leaves it without groups, so the test
makes them from the keyboard over a private copy of drive C:, on a desktop
of 1024x768 with the picture in memory:

  groups   File > New makes the group First with the items Notepad and File
           Manager (DumpIcon, USER.459, files their icons) and an empty group
           Second, which is then the active group on top (the dialog of
           File > New gives no focus back to First when it ends); Window > 1
           brings First back, and each item shows its icon with its title
           below it (16-bit OffsetRect places the title)
  covered  File Manager, started from File > Run, covers both group windows
           maximized; once it is closed they look as before, borders and the
           line under the caption included
  run      File > Run starts Notepad (an empty working directory through
           SHELL.DLL)

Every step waits for the menu, dialog or window it needs and every picture
for a quiet machine, not for a time.

    build/analysis-venv/bin/python tools/test_progman_host.py [--no-build] [--keep] [--kms KMS]
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
from test_support import active_caption_pair  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/progman-test'
# Program Manager's first window on a 1024x768 desktop, its group First and
# the part of Second beside it; the items of First, relative to the screen.
FRAME = (64, 54, 736, 536)
FIRST = (68, 98, 582, 382)
SECOND_EDGE = (582, 120, 604, 404)
ITEMS = (95, 172)
ICON_TOP, TITLE_TOP = 123, 157
# Points of the captions of First and of Second where both show.
FIRST_CAPTION, SECOND_CAPTION = ((300, 110), (500, 110)), ((300, 131), (500, 131))


def colours(picture, left, top, right, bottom):
    width, _, data = picture
    return {bytes(data[(y * width + x) * 3:(y * width + x) * 3 + 3])
            for y in range(top, bottom) for x in range(left, right)}


def caption_active(picture, points):
    width, _, data = picture
    left, right = (bytes(data[(y * width + x) * 3:(y * width + x) * 3 + 3]).hex() for x, y in points)
    return active_caption_pair(left, right)


def area(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return [bytes(data[(y * width + left) * 3:(y * width + right) * 3]) for y in range(top, bottom)]


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
    # Each step waits for the menu, the dialog or the window it needs (a
    # group window is a child of the MDI client, which FindWindow does not
    # see: a new group waits for a quiet machine), and each picture for a
    # quiet machine. File > New asks for the properties
    # of a group at once while there is none; once a group is there it asks
    # what to make first, where Enter takes Program Item and Right picks
    # Program Group.
    def menu(key, item):
        return [f'0:alt={key}', '0:wait=#32768', f'0:type={item}']

    def dialog(title):
        return [f'0:wait=#32770|{title}', '0:idle=300']

    def item(name, command):
        return (menu('46', 'n') + dialog('New Program Object') + ['0:key=0d'] +
                dialog('Program Item Properties') + [f'0:type={name}', '0:key=09', f'0:type={command}', '0:key=0d',
                                                     '0:idle=500'])

    def run(command):
        return menu('46', 'r') + dialog('Run') + [f'0:type={command}', '0:key=0d']

    steps = (['0:wait=Progman', '0:idle=1000'] +
             menu('46', 'n') + dialog('Program Group Properties') + ['0:type=First', '0:key=0d', '0:idle=1000'] +
             item('Notepad', 'NOTEPAD.EXE') + item('File Manager', 'WINFILE.EXE') +
             menu('46', 'n') + dialog('New Program Object') + ['0:key=27', '0:key=0d'] +
             dialog('Program Group Properties') + ['0:type=Second', '0:key=0d', '0:idle=1000',
                                                   '0:shot={out}/second.png'] +
             menu('57', '1') + ['0:idle=1000', '0:shot={out}/groups.png'] +
             run('WINFILE.EXE') + ['0:wait=WFS_Frame', '0:idle=1500', '0:shot={out}/winfile.png',
                                   '0:click=1010,12', '0:idle=1500', '0:shot={out}/covered.png'] +
             run('NOTEPAD.EXE') + ['0:wait=Notepad', '0:idle=1000', '0:shot={out}/notepad.png', '0:quit=1'])
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\PROGMAN.EXE', '--time-limit', '240'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=300)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    text = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(text)
    stops = [line for line in text.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    timed_out = [line for line in text.splitlines() if 'timed out after' in line]
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None
                for name in ('second', 'groups', 'winfile', 'covered', 'notepad')}
    groups, covered, second = pictures['groups'], pictures['covered'], pictures['second']
    checks = {
        f'every menu, dialog, window and quiet moment came before the time limit: {timed_out} {result.returncode}':
            not timed_out and result.returncode == 0,
        'groups: File > New makes the groups and items without a missing gate': not stops and groups is not None,
        'groups: the new group Second is the active group on top':
            second is not None and caption_active(second, SECOND_CAPTION) and
            not caption_active(second, FIRST_CAPTION),
        'groups: Window > 1 makes First the active group again':
            groups is not None and caption_active(groups, FIRST_CAPTION),
        'groups: each item of First shows its icon':
            groups is not None and all(len(colours(groups, x, ICON_TOP, x + 32, ICON_TOP + 32)) >= 4 for x in ITEMS),
        'groups: each title stands below its icon':
            groups is not None and all(len(colours(groups, x - 12, TITLE_TOP, x + 44, TITLE_TOP + 12)) >= 2
                                       for x in ITEMS),
        'covered: File Manager starts from File > Run': 'NEO98 KMS input wait=WFS_Frame' in text,
        'covered: File Manager covers the group windows': pictures['winfile'] is not None and
            area(pictures['winfile'], FIRST) != area(groups, FIRST) if groups else False,
        'covered: once File Manager is closed the group windows look as before':
            groups is not None and covered is not None and
            all(area(groups, box) == area(covered, box) for box in (FRAME, FIRST, SECOND_EDGE)),
        'run: File > Run starts Notepad': 'NEO98 KMS input wait=Notepad' in text,
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'progman host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
