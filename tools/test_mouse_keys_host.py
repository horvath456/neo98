#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""MouseKeys moves the pointer with the keypad, on neo98-kms.

SPI_SETMOUSEKEYS kept the settings of MouseKeys without acting on input.
tools/mouse_keys_fixture.c turns MouseKeys on for the digits of the keypad
and counts the presses of its buttons; the pointer starts at 300,300:

  move    6 three times and 2 once move the pointer right and down
  click   5 clicks the left button, + double clicks it
  right   - chooses the right button, and 5 clicks it

Before, the keys were digits for the window and the pointer stayed.

    build/analysis-venv/bin/python tools/test_mouse_keys_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
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

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/mouse-keys'


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/mouse_keys_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'MOUSEKEY.EXE')], check=True)
    keys = ['66', '66', '66', '62', '65', '6b', '6d', '65', '1b']
    steps = ['0:wait=MouseKeysTest', '1:move=300,300'] + [f'{2 + i * 0.3:.1f}:key={key}' for i, key in enumerate(keys)]
    steps.append(f'{2 + len(keys) * 0.2:.1f}:quit=1')
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\MOUSEKEY.EXE', '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=120)
        answer = win98_c.path('C:\\MOUSEKEY.TXT', drive)
        values = [int(v) for v in answer.read_text().split()] if answer else []
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(result.stdout + result.stderr)
    x, y, left, right, doubled = values if len(values) == 5 else (0, 0, 0, 0, 0)
    checks = {
        f'the fixture got the keys: {values}': len(values) == 5,
        f'move: 6 three times and 2 move the pointer right and down to {x},{y}': x >= 303 and y >= 301,
        f'click: 5 and + press the left button three times, one a double click: {left}, {doubled}':
            left + doubled == 3 and doubled == 1,
        f'right: - chooses the right button, 5 clicks it: {right}': right == 1,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'mouse keys: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
