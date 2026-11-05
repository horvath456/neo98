#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The font and base units of a 16-bit dialog template.

tools/dialog_font_win16_fixture.asm, a program that expects Windows 3.10,
opens two dialogs from a DS_SETFONT template with "MS Sans Serif" 8. USER asks for FW_BOLD for a standard template unless the style has
DS_3DLOOK (which every 4.0 module gets, ). SSERIFE.FON has no bold
face, so GDI simulates it : every character one pixel wider,
each row ORed one pixel to the right. The base units of the bold dialog are
7 and 13 instead of 6 and 13, so the dialog is wider and its text is bold.

Wise's uninstaller (UNWISE.EXE of Wonderland) depends on it: it gives its
controls a non-bold font of its own, and its German texts fit the statics
only at the bold base units.

    build/analysis-venv/bin/python tools/test_win16_dialog_font_host.py [--no-build]

Results in build/win16-dialog-font/results.json, screenshot dialogs.ppm.
"""
from __future__ import annotations

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
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402

# Template width 200 dialog units, frame of DS_MODALFRAME 3 pixels per side.
BOLD_WIDTH = 200 * 7 // 4 + 6
PLAIN_WIDTH = 200 * 6 // 4 + 6
TEXT = 'Wise Installation Wizard'


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def text_box(image, rect):
    """Bounding box and count of the black pixels inside the client area."""
    body, width = image
    x, y, w, h = rect
    found = [(px, py) for py in range(y + 24, y + h - 4) for px in range(x + 4, x + w - 4)
             if body[(py * width + px) * 3:(py * width + px) * 3 + 3] == b'\0\0\0']
    if not found:
        return 0, 0, 0
    xs = [p[0] for p in found]
    ys = [p[1] for p in found]
    return max(xs) - min(xs) + 1, max(ys) - min(ys) + 1, len(found)


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-dialog-font')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-c', '-R', str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    win16_fixture.assemble(ROOT / 'tools/dialog_font_win16_fixture.asm', out / 'DLGFONT.EXE', 'DLGFONT',
                           neo98.tool_environment())
    shutil.copyfile(out / 'DLGFONT.EXE', drive / 'DLGFONT.EXE')
    shot = out / 'dialogs.ppm'
    steps = ['0:wait=#32770|Bold16', '0:wait=#32770|Plain16', '2:shot=' + str(shot), '1:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'),
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\DLGFONT.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    shutil.rmtree(drive, ignore_errors=True)
    text = (out / 'log.txt').read_bytes().decode('latin-1')
    dialogs = [tuple(int(v) for v in m.groups()) for m in
               re.finditer(r'CreateWindow class=#32770 style=\w+ parent=\w+ x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)', text)]
    image = pixels(shot) if shot.exists() else None
    bold = text_box(image, dialogs[0]) if image and len(dialogs) == 2 else (0, 0, 0)
    plain = text_box(image, dialogs[1]) if image and len(dialogs) == 2 else (0, 0, 0)
    widths = [d[2] for d in dialogs]
    checks = {
        'both dialogs came up and the screenshot exists': len(dialogs) == 2 and image is not None,
        f'the 3.x template without DS_3DLOOK has base unit 7: {BOLD_WIDTH} pixels wide':
            widths[:1] == [BOLD_WIDTH],
        f'the template with DS_3DLOOK keeps base unit 6: {PLAIN_WIDTH} pixels wide': widths[1:2] == [PLAIN_WIDTH],
        # Simulated bold: one pixel more per character and about twice the ink.
        f'the bold text is one pixel wider per character':
            plain[0] > 0 and bold[0] - plain[0] >= len(TEXT) - 4,
        'the bold text has the doubled strokes': plain[2] > 0 and bold[2] >= plain[2] * 3 // 2,
        'both texts stand on one line of the same height': bold[1] == plain[1] and 0 < plain[1] <= 13,
    }
    (out / 'results.json').write_text(json.dumps({'dialogs': dialogs, 'bold_text': bold, 'plain_text': plain,
                                                  'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    dialogs:', dialogs, 'bold text:', bold, 'plain text:', plain, '\n    log:', out / 'log.txt')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
