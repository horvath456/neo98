#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A window that paints its own caption keeps it.

Windows paints a frame only through WM_NCPAINT and WM_NCACTIVATE of the
window, so a program that lets DefWindowProc paint and then draws over the
caption through GetWindowDC keeps its caption until the frame is painted
again, and then gets WM_NCPAINT and draws it again. Neo98's composition
painted every frame itself whenever the screen was composed, and the
program's caption was gone with the next composition.

tools/own_caption_fixture.c fills its caption magenta after DefWindowProc;
it invalidates its client, opens a popup over part of the caption (which
takes the activation) and closes it again. The caption is measured in four
pictures, outside the popup.

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


def magenta(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    width = int(size.split()[0])
    left, top, right, bottom = CAPTION
    outside = [(x, y) for y in range(top, bottom) for x in range(left, right)
               if not (COVER[0] <= x < COVER[2] and COVER[1] <= y < COVER[3])]
    hits = sum(1 for x, y in outside if body[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\xff\x00\xff')
    return hits, len(outside)


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
    steps.append('4.5:menu=quit')
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
    shutil.rmtree(drive, ignore_errors=True)
    checks, counts = {}, {}
    for name in PICTURES:
        path = out / f'{name.replace(" ", "-")}.ppm'
        hits, total = magenta(path) if path.is_file() else (0, 1)
        counts[name] = [hits, total]
        checks[f'{name}: the caption is the program\'s, {hits} of {total} pixels'] = hits == total
    (out / 'results.json').write_text(json.dumps({'counts': counts, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    pictures and log in', out)
    print(f'own caption host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
