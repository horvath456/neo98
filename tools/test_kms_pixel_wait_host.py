#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The input script of neo98-kms waits for a pixel.

wait=X,Y,RRGGBB in NEO98_HOST_INPUT holds the script until that pixel of the
screen has about that colour (within 48 of each channel), as the input
scripts of the other hosts do, and stderr gets the time since the step
before ("NEO98 KMS input pixel 96,108 is 000000 after 0.012 s"). neo98-kms
took the step for the name of a window class that never came, and the
script stopped there. Over a copy of setup's drive C:, Notepad starts; the
script waits for its white edit, types "MMMM" and waits for the stem of the
fourth M before it takes the picture.

wait=CLASS|TITLE waits for a window of that class with that title, as in the
other hosts (an empty class is any): a test waits for the title a program
gives its window when it has come so far. idle=MS waits until no thread has
been runnable for MS milliseconds, as before a picture in the other hosts;
here after Notepad has taken "MMMM".

    build/analysis-venv/bin/python tools/test_kms_pixel_wait_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import re
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
WORK = ROOT / 'build/kms-pixel-wait'
# Notepad at its first place on 1024x768: a point of its edit, and the stem
# of the fourth character of "MMMM" in its first line.
EDIT = (400, 300)
FOURTH_M = (96, 108)


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
    shot = WORK / 'typed.png'
    steps = ['0:wait=Notepad', '0:wait=Notepad|Untitled - Notepad', '0:wait=|Untitled - Notepad',
             f'0:wait={EDIT[0]},{EDIT[1]},ffffff', '1:type=MMMM', '1:idle=300',
             f'1:wait={FOURTH_M[0]},{FOURTH_M[1]},000000', f'1:shot={shot}', '2:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\WINDOWS\\NOTEPAD.EXE', '--time-limit', '40'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=100)
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    text = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(text)
    waits = re.findall(r'NEO98 KMS input pixel (\d+),(\d+) is ([0-9a-f]{6}) after ([\d.]+) s', text)
    picture = read_png(shot) if shot.is_file() else None
    stem = picture and bytes(picture[2][(FOURTH_M[1] * picture[0] + FOURTH_M[0]) * 3:][:3])
    checks = {
        'Notepad starts': 'NEO98 KMS input wait=Notepad\n' in text,
        'the script waits for Notepad by its class and title, and by its title alone':
            'NEO98 KMS input wait=Notepad|Untitled - Notepad' in text and
            'NEO98 KMS input wait=|Untitled - Notepad' in text,
        'the script waits for a quiet machine after typing': 'NEO98 KMS input idle=300 reached after' in text,
        f'the script waits for the white edit and says after how long: {waits[:1]}':
            waits[:1] and waits[0][:3] == (str(EDIT[0]), str(EDIT[1]), 'ffffff'),
        f'it waits for the fourth M after typing: {waits[1:2]}':
            waits[1:2] and waits[1][:3] == (str(FOURTH_M[0]), str(FOURTH_M[1]), '000000'),
        f'the picture after the wait shows the fourth M: {stem.hex() if stem else None}': stem == b'\0\0\0',
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Picture and log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'kms pixel wait: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
