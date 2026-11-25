#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The frames MFC's control bars paint themselves, and WordPad's toolbars.

tools/frame_paint_fixture.c draws three things on the host:
- a child whose frame comes from WM_NCCALCSIZE and is erased through its
  window DC with WM_ERASEBKGND: the frame is there after the child was shown
  (ShowWindow keeps the client the window answered), and DefWindowProc fills
  the clip box of that DC, so the frame is red;
- ExtTextOut with ETO_OPAQUE and a negative width fills the ordered
  rectangle (green);
- a dock whose child reaches into the dock's frame: the child is cut to the
  dock's client, so the dock's window DC paints its whole frame (yellow).
- a dialog with DS_3DLOOK turns the WS_BORDER of its edit into the sunken
  WS_EX_CLIENTEDGE, as the Shortcut page's fields have it.

Then WordPad (MFC 4.2): the borders of its two toolbars and of the dock bar
around them are painted, so no pixel of the teal desktop shows through
anywhere in its toolbar area.

    build/analysis-venv/bin/python tools/test_frame_paint_host.py [--no-build]

Results in build/frame-paint-host/results.json.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

RED, GREEN, YELLOW, TEAL = b'\xff\x00\x00', b'\x00\xff\x00', b'\xff\xff\x00', b'\x00\x80\x80'


def read_ppm(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width, height = (int(v) for v in size.split())
    return width, height, body


def count(path, colour, box=None):
    width, height, body = read_ppm(path)
    x0, y0, x1, y1 = box or (0, 0, width, height)
    return sum(body[(y * width + x) * 3:(y * width + x) * 3 + 3] == colour
               for y in range(max(y0, 0), min(y1, height)) for x in range(max(x0, 0), min(x1, width)))


def run(binary, out, name, exe, steps):
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    if exe.exists():
        shutil.copyfile(exe, drive / exe.name)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    program = 'C:\\' + exe.name if exe.exists() else str(exe)
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree', '--exe', program, '--quiet',
               '--drive-c', str(drive), '--no-registry-file', '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'{name}.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=600).returncode
    shutil.rmtree(drive, ignore_errors=True)
    return code, log_path.read_text(errors='replace')


def main():
    out = neo98.output_directory('frame-paint-host')
    for name in ('settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    exe = out / 'FRAMEPNT.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/frame_paint_fixture.c'), '-O1', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lgdi32', '-lkernel32', '-o', str(exe)], check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    shots = {'fixture': out / 'fixture.ppm', 'wordpad': out / 'wordpad.ppm'}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    fixture_code, fixture_log = run(binary, out, 'fixture', exe,
                                    ['0:wait=FramePaint', '0:wait=|Edge sunken', '0:idle=2000',
                                     f'1:shot={shots["fixture"]}', '2:menu=quit'])
    wordpad_code, wordpad_log = run(binary, out, 'wordpad', Path('C:\\Program Files\\Accessories\\WORDPAD.EXE'),
                                    ['0:wait=|Document - WordPad', '0:idle=3000', '1:windows=1',
                                     f'1:shot={shots["wordpad"]}', '2:menu=quit'])
    frame = re.search(r'^NEO98 HOST seamless item id=\S+ frame=(-?\d+),(-?\d+),(\d+),(\d+) .*title=Document - WordPad$',
                      wordpad_log, re.M)
    figures = {}
    if shots['fixture'].exists():
        # Below the caption, whose Windows icon has red, green and yellow too.
        client = (100, 125, 500, 400)
        figures.update(red=count(shots['fixture'], RED, client), green=count(shots['fixture'], GREEN, client),
                       yellow=count(shots['fixture'], YELLOW, client))
    if shots['wordpad'].exists() and frame:
        x, y, width, height = (int(v) for v in frame.groups())
        # The menu bar ends about 40 pixels below the top of the frame; the two
        # toolbars and the ruler follow.
        figures['wordpad_teal'] = count(shots['wordpad'], TEAL, (x + 4, y + 40, x + width - 4, y + 130))
    checks = {
        'the fixture ran': (fixture_code == 0 and shots['fixture'].exists()
                            and 'input wait=FramePaint reached' in fixture_log),
        'the frame from WM_NCCALCSIZE outlives the show and is erased red (200x60 less 188x48)':
            figures.get('red') == 200 * 60 - 188 * 48,
        'ExtTextOut fills a rectangle of negative width': figures.get('green') == 30 * 20,
        "the dock's window DC paints its whole frame above the bar": figures.get('yellow') == 300 * 4,
        'WS_BORDER of a control in a DS_3DLOOK dialog becomes WS_EX_CLIENTEDGE':
            'input wait=|Edge sunken reached' in fixture_log,
        'WordPad ran': (wordpad_code == 0 and shots['wordpad'].exists() and frame is not None
                        and 'input wait=|Document - WordPad reached' in wordpad_log),
        'no desktop shows through the toolbar area of WordPad': figures.get('wordpad_teal') == 0,
    }
    (out / 'results.json').write_text(json.dumps({'figures': figures, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'FRAME PAINT FAIL: {figures}; see {out}')
        return 1
    print('FRAME PAINT PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
