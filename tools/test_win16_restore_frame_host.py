#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A 16-bit window created minimized keeps its frame when it is restored.

tools/restore_frame_win16_fixture.asm creates its window with WS_MINIMIZE and
a menu and restores it with ShowWindow SW_SHOWNORMAL, as the 16-bit
Minesweeper does; its window procedure hands WM_NCCALCSIZE to DefWindowProc.
The answer of DefWindowProc went into the native structure only, and when
the procedure returned, the delivery copied the untouched 16-bit copy over
it: the client rectangle was the whole window, without caption and menu bar.

The program writes its window and client rectangles into C:\\RESTOR16.TXT;
the screenshot shows the caption and the menu bar above the white client.

    build/analysis-venv/bin/python tools/test_win16_restore_frame_host.py [--no-build]

Results in build/win16-restore-frame/results.json, screenshot restored.ppm.
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


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def pixel(image, x, y):
    body, width = image
    at = (y * width + x) * 3
    return tuple(body[at:at + 3])


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-restore-frame')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    win16_fixture.assemble(ROOT / 'tools/restore_frame_win16_fixture.asm', out / 'RESTOR16.EXE', 'RESTOR16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'RESTOR16.EXE', drive / 'RESTOR16.EXE')
    shot = out / 'restored.ppm'
    steps = ['0:wait=Restore16|Restore16', '2:shot=' + str(shot), '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\RESTOR16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\RESTOR16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    image = pixels(shot) if shot.exists() else None
    complete = len(data) >= 24 and image is not None
    left, top, right, bottom, _, _, width, height, x, y = struct.unpack_from('<10h', data) if complete else (0,) * 10
    style = struct.unpack_from('<I', data, 20)[0] if complete else 0
    # Caption and menu bar lie between the top of the window and the client
    # origin; only the frame lies to the left, right and below it.
    above, below = y - top, bottom - (y + height)
    side_left, side_right = x - left, right - (x + width)
    caption = pixel(image, (left + right) // 2, top + 8) if complete else None
    inside = pixel(image, x + width // 2, y + height // 2) if complete else None
    menu_ink = sum(1 for py in range(max(top, y - 18), y) for px in range(left + 4, left + 60)
                   if pixel(image, px, py) == (0, 0, 0)) if complete else 0
    checks = {
        'the program wrote its rectangles and the screenshot exists': complete,
        'the restored window is no longer minimized': complete and not style & 0x20000000,
        'the window has its CreateWindow size': (right - left, bottom - top) == (240, 160),
        f'caption and menu bar above the client: {above} pixels': above >= 30,
        f'only the frame left, right and below: {side_left}, {side_right}, {below} pixels':
            0 < side_left == side_right == below <= 8,
        f'the caption is drawn, not the white client: {caption}': caption is not None and caption != (255, 255, 255),
        f'the client is white: {inside}': inside == (255, 255, 255),
        f'the menu bar shows its item: {menu_ink} black pixels': menu_ink >= 10,
    }
    (out / 'results.json').write_text(json.dumps({'window': [left, top, right, bottom], 'client': [width, height],
                                                  'origin': [x, y], 'style': style, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    written:', data.hex(), '\n    log:', out / 'log.txt')
        return 1
    print('win16 restore frame: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
