#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A control under a disabled window draws by its own state.

Windows disables the owner of a modal dialog, and the controls of the owner
keep looking as they did: IsWindowEnabled answers the window's own
WS_DISABLED, and each control draws greyed only when it is disabled itself.
The port answered for the window and all its parents, so File Manager's
drive box looked disabled while a dialog was open, and stayed grey after the
dialog closed until it was repainted.

tools/ancestor_disabled_fixture.c disables its window with a push button,
an edit, a drop-down list, a list box, a static and a check box, and repaints
it. On neo98-kms with the picture of a 1024x768 desktop in memory:

  answer  IsWindowEnabled is FALSE for the window and TRUE for each control
  look    the texts of the push button, the check box, the list box and the
          drop-down list are black, none grey, and the drop-down list keeps
          the window colour behind its text

    build/analysis-venv/bin/python tools/test_ancestor_disabled_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
    print('SKIP: needs i686-w64-mingw32-gcc')
    _sys.exit(77)
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/ancestor-disabled'
# The texts of the controls on the screen: the window stands at 100,100 and
# its client area begins at 104,123.
TEXTS = {'push button': (130, 140, 200, 158), 'check box': (320, 135, 380, 152), 'edit': (117, 176, 200, 195),
         'drop-down list': (117, 216, 200, 232), 'list box': (117, 256, 200, 272)}
COMBO_BACKGROUND = (220, 224)
BLACK, GREY, WHITE = (0, 0, 0), (128, 128, 128), (255, 255, 255)


def count(picture, box, colour):
    width, _, data = picture
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3]) == colour)


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ancestor_disabled_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'ENABLED.EXE')], check=True)
    steps = ['0:wait=AncestorDisabled', '3:shot={out}/disabled.png', '4:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\ENABLED.EXE', '--time-limit', '20'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=110)
        written = win98_c.path('C:\\ENABLED.TXT', drive)
        data = written.read_bytes() if written else b''
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(result.stdout + result.stderr)
    answers = struct.unpack('<7i', data) if len(data) >= 28 else None
    picture = read_png(WORK / 'disabled.png') if (WORK / 'disabled.png').is_file() else None
    checks = {
        'the program wrote its answers and the picture exists': answers is not None and picture is not None,
        f'answer: IsWindowEnabled is FALSE for the window and TRUE for each control: {answers}':
            answers is not None and answers[0] == 0 and all(answers[1:]),
    }
    for name, box in TEXTS.items():
        black = count(picture, box, BLACK) if picture else 0
        grey = count(picture, box, GREY) if picture else -1
        checks[f'look: the {name} draws its text black, none grey: {black} black, {grey} grey'] = black > 10 and grey == 0
    behind = tuple(picture[2][(COMBO_BACKGROUND[1] * picture[0] + COMBO_BACKGROUND[0]) * 3:][:3]) if picture else None
    checks[f'look: the drop-down list keeps the window colour behind its text: {behind}'] = behind == WHITE
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Picture and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'ancestor disabled host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
