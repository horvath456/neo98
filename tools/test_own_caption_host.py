#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows that paint their own frame keep it; WM_NCPAINT carries the region.

Windows paints a frame only through the window: DefWindowProc of WM_NCPAINT
and WM_NCACTIVATE, in the window's thread. A program that lets DefWindowProc
paint and then draws over its caption through GetWindowDC keeps its caption
until the frame has to be painted again; then it gets WM_NCPAINT with the
update region in screen coordinates (1 when the whole window is to be
painted) and draws it again. A window that answers WM_NCPAINT and
WM_NCACTIVATE without DefWindowProc has no frame at all. Neo98's composition
painted every frame itself, outside any message, and WM_NCPAINT always
carried 1.

tools/own_caption_fixture.c: a window that fills its caption magenta after
DefWindowProc invalidates its client, opens a popup over part of the
caption (which takes the activation) and closes it again, then shows it
once more without the activation and closes it; a second window
never lets DefWindowProc paint its frame. The caption is measured in four
pictures, outside the popup; the second window's caption area keeps the
desktop's colour; the first window writes down what each WM_NCPAINT
carried and the colour at a point of its caption under the popup before
DefWindowProc painted.

    build/analysis-venv/bin/python tools/test_own_caption_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402

# The window stands at 40,40 with 300 by 200 pixels; its caption lies inside
# the sizing border of 4, 18 pixels high. The popup covers 100..250 x 30..90.
CAPTION = (45, 45, 335, 62)
COVER = (100, 30, 250, 90)
PICTURES = {'shown': 0.6, 'client repainted': 1.3, 'popup over it': 2.3, 'popup closed': 3.8}
# The caption rows of the window without a frame (360,40 of 200 by 150),
# which it shows only after the first picture, and a point of the desktop.
BARE = (366, 46, 554, 60)
DESKTOP = (620, 250)
# The part of the first window the popup covered, in screen coordinates.
EXPOSED = (100, 40, 250, 90)


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width = int(size.split()[0])
    return lambda x, y: body[(y * width + x) * 3:(y * width + x) * 3 + 3]


def magenta(path):
    at = pixels(path)
    left, top, right, bottom = CAPTION
    outside = [(x, y) for y in range(top, bottom) for x in range(left, right)
               if not (COVER[0] <= x < COVER[2] and COVER[1] <= y < COVER[3])]
    hits = sum(1 for x, y in outside if at(x, y) == b'\xff\x00\xff')
    return hits, len(outside)


def bare(path, before):
    # The same pixels as before the window was shown: the desktop.
    at, was = pixels(path), pixels(before)
    left, top, right, bottom = BARE
    area = [(x, y) for y in range(top, bottom) for x in range(left, right)]
    return sum(1 for x, y in area if at(x, y) == was(x, y)), len(area), was(*DESKTOP)


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('own-caption')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/own_caption_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-lgdi32',
                    '-o', str(drive / 'OWNCAP.EXE')], env=neo98.tool_environment(), check=True)
    steps = ['0:wait=|OwnCaption', '0.3:move=600,450']
    steps += [f'{when}:shot={out}/{name.replace(" ", "-")}.ppm' for name, when in PICTURES.items()]
    steps.append('5:menu=quit')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='30',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\OWNCAP.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(out / 'c.registry'), '--crash-report-dir', str(out / 'reports')],
                       cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\OWNCAP.TXT', drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    checks, counts = {}, {}
    for name in PICTURES:
        path = out / f'{name.replace(" ", "-")}.ppm'
        hits, total = magenta(path) if path.is_file() else (0, 1)
        counts[name] = [hits, total]
        checks[f'{name}: the caption is the program\'s, {hits} of {total} pixels'] = hits == total
    last = out / 'popup-closed.ppm'
    first = out / 'shown.ppm'
    hits, total, desktop = bare(last, first) if last.is_file() and first.is_file() else (0, 1, b'')
    counts['no frame'] = [hits, total]
    checks[f'the window without DefWindowProc has no frame, the desktop shows: {hits} of {total} pixels'] = \
        hits == total
    paints = [line.split()[1:] for line in lines if line.startswith('ncpaint ')]
    checks[f'the first WM_NCPAINT, the window shown, carries 1: {paints[:1]}'] = bool(paints) and paints[0][0] == '1'
    boxes = [tuple(int(v) for v in p[:4]) for p in paints if len(p) == 5]
    checks[f'the popup gone, WM_NCPAINT carries the region it uncovered, in screen coordinates: {boxes}'] = \
        EXPOSED in boxes
    # Before DefWindowProc the point under the popup shows the desktop (the
    # first paint), the popup's white client or the program's magenta;
    # never a caption USER painted there itself. The popup shown without the
    # activation leaves its white there for the WM_NCPAINT alone.
    background = '%06x' % int.from_bytes(desktop[::-1], 'big') if desktop else ''
    seen = sorted({p[-1] for p in paints})
    checks[f'nothing painted the caption before DefWindowProc: {seen}'] = \
        bool(paints) and set(seen) <= {background, 'ffffff', 'ff00ff'}
    alone = [p for p in paints if len(p) == 5 and tuple(int(v) for v in p[:4]) == EXPOSED and p[4] == 'ffffff']
    checks[f'the popup gone without an activation, its white was still there for WM_NCPAINT: {len(alone)}'] = \
        bool(alone)
    (out / 'results.json').write_text(json.dumps({'counts': counts, 'lines': lines, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    pictures and log in', out)
    print(f'own caption host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
