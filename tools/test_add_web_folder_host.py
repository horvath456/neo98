#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Add Web Folder opens without the sunken box over its text.

The first page of the wizard (MSOWS409.DLL, dialog 1300) has an SS_ETCHEDHORZ
static, which USER gives WS_EX_STATICEDGE, and below it in Z order a
label with the same rectangle that covers it on its first WM_PAINT. USER
draws a frame in WM_NCPAINT, which BeginPaint sends on the window's own
thread just before the content; the port drew the frame of the etched static
at the next composition and the label when the dialog's thread was free, and
the box stood alone over the text for a while (src/user.cpp
PaintWindow, PaintControlSurface). The test takes a picture every 30 ms
after the double click: none may show the box, and the sunken separator
above the buttons, a frame of its own, is there.
On the desktop of the first logon, without a window.

    build/analysis-venv/bin/python tools/test_add_web_folder_host.py [--no-build]
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import first_logon  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

FRAMES = 60
# The dialog at 64,55: the top edge of the etched static's box at y 275, the
# separator above Back, Next and Cancel at y 328, both through x 300.
BOX = (300, 275)
SEPARATOR = (300, 328)
SHADOW = (128, 128, 128)


def pixel(path, point):
    data = path.read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    x, y = point
    return tuple(parts[3][(y * width + x) * 3:(y * width + x) * 3 + 3])


def main():
    out = neo98.output_directory('add-web-folder-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive, registry = first_logon.clone(out / 'session')
    # My Computer at 38,22 on the desktop; Web Folders at 332,265 in its
    # window; Add Web Folder at 257,190 in that folder.
    steps = ['0:wait=Shell_TrayWnd', '1:dblclick=38,22', '4:wait=CabinetWClass|My Computer', '6:dblclick=332,265',
             '10:dblclick=257,190']
    steps += [f'{10.3 + i * 0.03:.2f}:shot={out}/f{i:02d}.ppm' for i in range(FRAMES)]
    steps += [f'14:shot={out}/end.ppm', '15:tops=1']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='25', NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_TEST_CAMERA='none')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    with (out / 'host.log').open('w') as log:
        code = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--registry', str(registry),
                               '--crash-report-dir', str(out / 'reports')], cwd=ROOT, env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=240).returncode
    text = (out / 'host.log').read_text(errors='replace')
    shots = [out / f'f{i:02d}.ppm' for i in range(FRAMES)] + [out / 'end.ppm']
    have = all(p.is_file() for p in shots)
    # A picture with the dialog: its separator is drawn.
    dialog = [p for p in shots if have and pixel(p, SEPARATOR) == SHADOW]
    boxed = [p.stem for p in dialog if pixel(p, BOX) == SHADOW]
    checks = {
        'all pictures taken': have,
        'the wizard is open, its sunken separator drawn': have and pixel(shots[-1], SEPARATOR) == SHADOW,
        'no picture shows the box of the etched static': have and bool(dialog) and not boxed,
        'no crash': 'NEO98 HOST failed' not in text and 'CRASHREPORT' not in text and code == 0,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass; {len(dialog)} pictures with the dialog, '
          f'box in {boxed}; log {out / "host.log"}')
    subprocess.run(['rm', '-rf', str(out / 'session')])
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
