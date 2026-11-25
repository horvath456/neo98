#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Colours of GDI drawing after a switch of the desktop to 16 bits per pixel.

tools/depth16_colours_fixture.c switches the screen to 800x600 with 16 bits,
whose layout is 5-5-5, and draws eight colours in nine ways: straight, from
a compatible bitmap, from DIB sections of 5-5-5, 5-6-5 (BI_BITFIELDS) and 24
bits, with SetDIBitsToDevice and StretchDIBits, and with StretchBlt from the
5-6-5 section. It also copies the compatible bitmap into a 5-6-5 section and
reads the words. neo98-kms runs it over a private copy of drive C: with the
picture in memory; the test checks the program's GetPixel rows and the middle
of every square on the screen, each component within 12 of the colour.

    build/analysis-venv/bin/python tools/test_depth16_colours_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
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
import neo98  # noqa: E402
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

WORK = ROOT / 'build/depth16-colours-test'
SIZE = 40
COLOURS = [(0, 128, 128), (255, 0, 0), (0, 255, 0), (0, 0, 255), (128, 64, 32), (255, 255, 255),
           (192, 192, 192), (255, 128, 0)]
WAYS = ['FillRect', 'compatible bitmap', '5-5-5 DIB section', '5-6-5 DIB section', '24-bit DIB section',
        'SetDIBitsToDevice', 'StretchDIBits 5-6-5', 'StretchDIBits 5-5-5', 'StretchBlt 5-6-5 section']


def near(a, b):
    return all(abs(x - y) <= 12 for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=ROOT / 'build/native-app/neo98-kms')
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    exe = WORK / 'DEPTH16.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib',
                    '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp', '-o', str(exe),
                    str(ROOT / 'tools/depth16_colours_fixture.c'), '-luser32', '-lgdi32', '-lkernel32'],
                   env=neo98.tool_environment(), check=True, stdin=subprocess.DEVNULL)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    shutil.copyfile(exe, drive / 'DEPTH16.EXE')
    shot = WORK / 'screen.png'
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KMS_OUTPUT='image:800x600',
               NEO98_HOST_INPUT=f'0:wait=Depth16Colours;3:shot={shot}')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--exe', 'C:\\DEPTH16.EXE',
                                 '--time-limit', '40'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=100)
        (WORK / 'log.txt').write_text(result.stdout + result.stderr)
        blob = (drive / 'DEPTH16.BIN').read_bytes() if (drive / 'DEPTH16.BIN').is_file() else b''
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    rows = list(struct.iter_unpack('<3I', blob[4:])) if len(blob) >= 4 else []
    colour = lambda v: (v & 255, (v >> 8) & 255, (v >> 16) & 255)  # noqa: E731
    checks = {}
    reverse = rows[:8]
    checks['a compatible bitmap copied into a 5-6-5 section holds the 5-6-5 words of the colours'] = \
        len(reverse) == 8 and all(near(colour(got), colour(want)) for _, got, want in reverse)
    picture = read_png(shot) if shot.is_file() else None
    for way, name in enumerate(WAYS):
        read = rows[8 + way * 8:16 + way * 8]
        if picture:
            width, _, data = picture
            screen = [tuple(data[((way * SIZE + SIZE // 2) * width + i * SIZE + SIZE // 2) * 3:][:3])
                      for i in range(8)]
        else:
            screen = []
        checks[f'{name}: GetPixel and the screen show the colours'] = \
            len(read) == 8 and all(near(colour(got), colour(want)) for _, got, want in read) and \
            len(screen) == 8 and all(near(seen, want) for seen, want in zip(screen, COLOURS))
        if not checks[f'{name}: GetPixel and the screen show the colours']:
            print(f'    {name}: GetPixel', ['%06x' % (got & 0xffffff) for _, got, _ in read],
                  'screen', ['%02x%02x%02x' % pixel for pixel in screen])
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if not all(checks.values()):
        print('    picture and log:', WORK)
        raise SystemExit(f'{sum(not v for v in checks.values())} of {len(checks)} checks failed')
    print(f'depth16 colours: all {len(checks)} checks passed')


if __name__ == '__main__':
    main()
