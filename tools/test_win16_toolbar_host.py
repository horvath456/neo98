#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The toolbar of the 16-bit COMMCTRL with the messages of a 16-bit program.

tools/toolbar_win16_fixture.asm fills one toolbar the way File Manager does:
CreateToolbarEx without bitmap and buttons, then TB_ADDBITMAP with
MAKELONG(instance, bitmap) and TB_ADDBUTTONS with twelve-byte TBBUTTONs. The
native toolbar got both unchanged: it found no bitmap in a 16-bit instance
and read the buttons at a flat address, and File Manager's buttons were
blank. The strip has two images and the program asks for three, as File
Manager asks for fourteen of thirteen; Windows enlarges such a strip with the
face colour, where the native toolbar refused the whole strip. A second
toolbar gets strip and buttons from CreateToolbarEx itself.

    build/analysis-venv/bin/python tools/test_win16_toolbar_host.py [--no-build]

Results in build/win16-toolbar/results.json, screenshot toolbars.ppm.
On Linux the host needs a display (xvfb-run).
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

RED, GREEN, FACE = (255, 0, 0), (0, 255, 0), (192, 192, 192)


def strip():
    """Bitmap 1: a device independent bitmap of 32 by 15 pixels, four bits a
    pixel, the left image red and the right one green."""
    palette = [(0, 0, 0), RED, GREEN] + [FACE] * 13
    header = struct.pack('<IiiHHIIiiII', 40, 32, 15, 1, 4, 0, 0, 0, 0, 0, 0)
    colours = b''.join(bytes((b, g, r, 0)) for r, g, b in palette)
    row = bytes([0x11] * 8 + [0x22] * 8)
    return header + colours + row * 15


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def centre(image, rect, origin):
    """The colour in the middle of an item rectangle of a toolbar."""
    body, width = image
    x = origin[0] + (rect[0] + rect[2]) // 2
    y = origin[1] + (rect[1] + rect[3]) // 2
    at = (y * width + x) * 3
    return tuple(body[at:at + 3])


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-toolbar')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    win16_fixture.assemble(ROOT / 'tools/toolbar_win16_fixture.asm', out / 'TOOLBR16.EXE', 'TOOLBR16',
                           neo98.tool_environment(), modules=('KERNEL', 'USER', 'COMMCTRL'),
                           resources=[(2, 1, strip())])
    shutil.copyfile(out / 'TOOLBR16.EXE', drive / 'TOOLBR16.EXE')
    shot = out / 'toolbars.ppm'
    steps = ['0:wait=Toolbar16|Toolbar16', '2:shot=' + str(shot), '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\TOOLBR16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\TOOLBR16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    image = pixels(shot) if shot.exists() else None
    complete = len(data) >= 64 and image is not None
    added, count = struct.unpack_from('<hH', data) if complete else (None, None)
    button = struct.unpack_from('<hhBBIh', data, 4) if complete else ()
    top = [struct.unpack_from('<4h', data, 16 + i * 8) for i in range(3)] if complete else []
    top_origin = struct.unpack_from('<2h', data, 40) if complete else (0, 0)
    bottom = [struct.unpack_from('<4h', data, 44 + i * 8) for i in range(2)] if complete else []
    bottom_origin = struct.unpack_from('<2h', data, 60) if complete else (0, 0)
    sized = complete and all(r[2] - r[0] >= 16 and r[3] - r[1] >= 15 for r in top + bottom)
    colours = [centre(image, r, top_origin) for r in top] if complete and sized else []
    bottom_colours = [centre(image, r, bottom_origin) for r in bottom] if complete and sized else []
    checks = {
        'the program wrote the answers and the screenshot exists': complete,
        f'TB_ADDBITMAP of MAKELONG(instance, 1) answers the first index: {added}': added == 0,
        f'TB_ADDBUTTONS took the three 16-bit buttons: {count}': count == 3,
        f'TB_GETBUTTON answers the 16-bit TBBUTTON: {button}': button == (1, 101, 4, 0, 0x12345678, -1),
        f'TB_GETITEMRECT answers the rectangles side by side: {top}':
            sized and top[0][2] <= top[1][0] and top[1][2] <= top[2][0],
        f'the first button shows the red image: {colours[:1]}': colours[:1] == [RED],
        f'the second button shows the green image: {colours[1:2]}': colours[1:2] == [GREEN],
        f'the third image, beyond the strip, is transparent: {colours[2:]}': colours[2:] == [FACE],
        f'CreateToolbarEx with strip and buttons shows both images: {bottom_colours}':
            bottom_colours == [RED, GREEN],
    }
    (out / 'results.json').write_text(json.dumps({'added': added, 'count': count, 'button': button,
                                                  'top': top, 'top_origin': top_origin, 'bottom': bottom,
                                                  'bottom_origin': bottom_origin, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    written:', data.hex(), '\n    log:', out / 'log.txt')
        return 1
    print('win16 toolbar: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
