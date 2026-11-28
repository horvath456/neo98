#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The cell of check boxes and radio buttons, and the setting that sizes it.

USER of Windows 95 and 98 gives the check boxes and radio buttons a cell of
13 by 13 pixels at every font size (docs/FONT-SIZE.md). That stays the rule;
with Neo98's setting HKEY_LOCAL_MACHINE\\Software\\NEO98,
CheckBoxesFollowFontSize = "1", the cell is 13 x dpi / 96, drawn at that
size by the Marlett cells of DrawFrameControl, the caption after it and the
focus rectangle around the caption.

tools/check_cell_fixture.c shows a checked box with the focus, a radio
button, a three-state box, a disabled box and one with BS_LEFTTEXT. The test
runs it at 96 dots per inch with the setting and at 144 without and with it,
and measures the cells in the pictures.

    build/analysis-venv/bin/python tools/test_check_cell_host.py [--no-build]
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
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from win98_folder import JOURNAL_SIGNATURE  # noqa: E402

FACE = b'\xc0\xc0\xc0'
# The colours a cell is drawn in with the standard scheme; a stretched
# picture would show others between them.
CELL_COLOURS = {b'\x00\x00\x00', b'\x80\x80\x80', b'\xc0\xc0\xc0', b'\xdf\xdf\xdf', b'\xff\xff\xff'}
BUTTONS = 5


def text_value(path, name, value):
    """A REG_SZ record of the registry journal (src/registry_persist.cpp)."""
    out = bytearray([3])
    for text in (path, name):
        raw = text.encode('cp1252')
        out += struct.pack('<H', len(raw)) + raw
    data = value.encode('cp1252') + b'\0'
    return bytes(out) + struct.pack('<II', 1, len(data)) + data


def run(binary, pristine, out, name, dpi, setting):
    folder = out / name
    folder.mkdir(parents=True)
    drive = folder / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    journal = folder / 'c.registry'
    body = win98_c.journal(pristine).read_bytes() if win98_c.journal(pristine).is_file() else JOURNAL_SIGNATURE
    settings = 'HKEY_LOCAL_MACHINE\\Config\\0001\\Display\\Settings'
    for value in ('DPILogicalX', 'DPILogicalY', 'DPIPhysicalX', 'DPIPhysicalY'):
        body += text_value(settings, value, str(dpi))
    if setting:
        body += text_value('HKEY_LOCAL_MACHINE\\Software\\NEO98', 'CheckBoxesFollowFontSize', '1')
    journal.write_bytes(body)
    shutil.copyfile(out / 'CHECKS.EXE', drive / 'CHECKS.EXE')
    shot = folder / 'cells.ppm'
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='30',
               NEO98_HOST_INPUT=f'0:wait=CheckCell;1:move=600,450;2:shot={shot};3:menu=quit',
               NEO98_SETTINGS_DIR=str(folder / 'settings'), NEO98_LOG_DIR=str(folder / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    with (folder / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\CHECKS.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(journal), '--crash-report-dir', str(folder / 'reports')],
                       cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    shutil.rmtree(drive, ignore_errors=True)
    if not shot.is_file():
        return None
    magic, size, depth, pixels = shot.read_bytes().split(b'\n', 3)
    return int(size.split()[0]), pixels


def measure(picture, index):
    """The cell of a button (its columns from the near edge on, its rows) and
    the first column of what follows it: the focus rectangle or the text."""
    width, pixels = picture
    top, bottom, left, right = 10 + 50 * index, 50 + 50 * index, 10, 310

    def pixel(x, y):
        return pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]

    def marked(x):
        return any(pixel(x, y) != FACE for y in range(top, bottom))

    leftwards = index == 4
    columns = range(right - 1, left - 1, -1) if leftwards else range(left, right)
    cell = []
    for x in columns:
        if marked(x):
            cell.append(x)
        elif cell:
            break
    if not cell:
        return None
    following = next((x for x in columns if (x < min(cell) if leftwards else x > max(cell)) and marked(x)), None)
    rows = [y for y in range(top, bottom) if any(pixel(x, y) != FACE for x in cell)]
    colours = {pixel(x, y) for x in cell for y in rows}
    return {'width': max(cell) - min(cell) + 1, 'height': max(rows) - min(rows) + 1, 'left': min(cell),
            'right': max(cell), 'following': following, 'colours': sorted(c.hex() for c in colours)}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('check-cell')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/check_cell_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-lgdi32',
                    '-o', str(out / 'CHECKS.EXE')], env=neo98.tool_environment(), check=True)
    cases = {'96 with the setting': (96, True), '144 without': (144, False), '144 with the setting': (144, True)}
    results = {}
    for name, (dpi, setting) in cases.items():
        picture = run(binary, pristine, out, name.replace(' ', '-'), dpi, setting)
        results[name] = [measure(picture, i) if picture else None for i in range(BUTTONS)]
    small, plain, large = results['96 with the setting'], results['144 without'], results['144 with the setting']
    kinds = ['check box', 'radio button', 'three-state box', 'disabled box', 'BS_LEFTTEXT box']

    def sizes(cells):
        return [(c['width'], c['height']) if c else None for c in cells]

    def cells_of(cell):
        # The box kinds fill their cell; the circle of a radio button leaves
        # the last row and column of it (12 of 13, 18 of 20).
        return [(cell, cell), (cell - 1 - cell // 20, cell - 1 - cell // 20)] + [(cell, cell)] * (BUTTONS - 2)

    checks = {
        'every picture shows the five cells': all(all(c for c in cells) for cells in results.values()),
        f'96 dots per inch, the setting on: 13 by 13, {sizes(small)}': sizes(small) == cells_of(13),
        f'144 dots per inch, the setting off: 13 by 13 as in Windows, {sizes(plain)}': sizes(plain) == cells_of(13),
        f'144 dots per inch, the setting on: 20 by 20 (13 x 144 / 96), {sizes(large)}':
            sizes(large) == cells_of(20),
    }
    if all(checks.values()):
        for i, kind in enumerate(kinds):
            if i == 4:
                continue
            moved = large[i]['following'] - plain[i]['following']
            checks[f'the {kind}\'s caption moves with the cell: {moved} pixels'] = moved == 7
        checks['the BS_LEFTTEXT cell stays at the right end'] = large[4]['right'] == plain[4]['right']
        focus = large[0]['following']
        checks[f'the focus rectangle frames the caption, after the cell: {focus} > {large[0]["right"]}'] = \
            focus > large[0]['right'] + 1
        foreign = sorted({colour for c in large for colour in c['colours']} -
                         {colour.hex() for colour in CELL_COLOURS})
        checks[f'the large cells are drawn, not stretched: no colours between the scheme\'s {foreign}'] = not foreign
    (out / 'results.json').write_text(json.dumps({'results': results, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    pictures and logs in', out)
    print(f'check cell host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
