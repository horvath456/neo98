#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The pointer stays on the screen over a window that blits through DirectDraw.

A windowed DirectDraw game blits its picture to the primary surface every
frame. The display driver's Blt publishes the screen surface, and the copy
took the pointer USER had drawn on the frame with it; the pointer came back
only with the next move of the mouse or the next paint of USER, so it blinked
over every such game.

The fixture (tools/ddraw_pointer_fixture.c) blits two colours in turn, none
of them white or black. The host puts the pointer over its client area and
takes pictures a tenth of a second apart while the mouse rests; every one of
them has to show the arrow.

    build/analysis-venv/bin/python tools/test_ddraw_pointer_host.py [host]
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c

SHOTS = 30
X, Y = 300, 250  # canvas pixels, inside the client area of the fixture


def arrow(path):
    """White and black pixels in the box the arrow covers at X, Y."""
    data = path.read_bytes()
    assert data[:15] == b'P6\n800 600\n255\n', data[:20]
    pixels, white, black = data[15:], 0, 0
    for y in range(Y, Y + 22):
        row = pixels[(y * 800 + X) * 3:(y * 800 + X + 14) * 3]
        for i in range(0, len(row), 3):
            white += row[i:i + 3] == b'\xff\xff\xff'
            black += row[i:i + 3] == b'\0\0\0'
    return white, black


def main():
    out = ROOT / 'build/ddraw-pointer'
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob('shot-*.ppm'):
        old.unlink()
    exe = out / 'DDPOINT.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_pointer_fixture.c'),
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-lgdi32', '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    source = win98_c.use_pristine()
    drive = out / 'fixture-c'
    drive.mkdir(exist_ok=True)
    binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/native-app/neo98-host'
    steps = ['0:wait=DDPointer', '2:move=%d,%d' % (X, Y)]
    at = 3.0
    for index in range(SHOTS):
        steps.append('%.2f:shot=%s' % (at, out / ('shot-%02d.ppm' % index)))
        at += 0.1
    steps.append('%.2f:menu=quit' % (at + 0.5))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_MENU='', NEO98_SETTINGS_DIR=str(out / 'settings'))
    result = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             '--system-tree', str(source), '--exe', 'E:\\DDPOINT.EXE',
                             '--drive-readonly', 'E=' + str(out), '--drive-c', str(drive),
                             '--no-registry-file', '--quiet'], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
    (out / 'host.log').write_text(result.stdout)
    assert 'DDPOINTER running' in result.stdout, result.stdout[-2200:]
    shots = sorted(out.glob('shot-*.ppm'))
    assert len(shots) == SHOTS, (len(shots), result.stdout[-2200:])
    seen = [arrow(shot) for shot in shots]
    bare = [index for index, (white, black) in enumerate(seen) if white < 30 or black < 20]
    assert not bare, f'{len(bare)} of {SHOTS} pictures show no pointer over the DirectDraw window: {bare} {seen}'
    print(f'DDRAW POINTER PASS: the arrow is in all {SHOTS} pictures over a window that blits every frame')


if __name__ == '__main__':
    main()
