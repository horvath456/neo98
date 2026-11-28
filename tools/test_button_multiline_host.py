#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Buttons of several lines (BS_MULTILINE) and a caption wider than its button.

USER measures a BS_MULTILINE caption with DrawText (DT_CALCRECT,
DT_WORDBREAK, DT_EDITCONTROL) in the content rectangle less SM_CXEDGE at both
sides and SM_CYBORDER above and below, places the block of lines like a
single line (BS_TOP, BS_BOTTOM, BS_VCENTER, BS_LEFT, BS_RIGHT, BS_CENTER) and
draws the lines in it with DT_WORDBREAK; the focus rectangle frames the
block, and the check cell keeps its own place (centred, or in the first or
last line with BS_TOP and BS_BOTTOM). A caption without BS_MULTILINE stays on
one line and is cut at the button's edge. Neo98 drew every caption on one
line.

tools/button_multiline_fixture.c shows ten such buttons and writes the rows
of ink of each, measured with the same program in Windows 98 SE (EXPECTED)
and Windows 95 OSR2 (which differs only in that the lines of a centred or
right-aligned block start at its left; Neo98's SetMultilineButtonLinesLeft).
The test runs it at 96 dots per inch and compares every row, and at 144 with
CheckBoxesFollowFontSize, where the cell is 20 pixels and the lines follow
it. The fixture's picture of its window is kept beside the results.

    build/analysis-venv/bin/python tools/test_button_multiline_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)

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
from test_check_cell_host import text_value  # noqa: E402
from win98_folder import JOURNAL_SIGNATURE  # noqa: E402

# Windows 98 SE in a virtual machine, at 96 dots per inch: the rows of the
# check cell and the ink of the caption area of each button, without the
# focus; then the bounding box of the third button's focus rectangle.
EXPECTED = {
    'b0.cell': '1,12',
    'b0.ink': '3-4:21-250 5-6:20-250 7-9:19-250 10-11:18-250 12-12:175-175 13-13:32-175 16-16:23-77 17-18:23-74 '
              '19-19:20-93 20-20:19-94 21-21:19-92 22-22:19-93 23-23:19-94 24-24:20-96 25-25:51-89 26-26:51-88',
    'b1.cell': '1,12',
    'b1.ink': '3-3:19-240 4-5:19-246 6-6:19-258 7-10:19-259 11-11:20-258 12-12:143-255 13-13:69-255 16-16:36-250 '
              '17-18:30-247 19-19:20-266 20-20:19-267 21-21:20-265 22-22:21-266 23-23:19-267 24-24:20-266 '
              '25-25:48-262 26-26:44-261 29-29:27-106 30-31:27-89 32-32:19-111 33-35:19-112 36-36:20-112 '
              '37-37:20-114 38-38:70-101 39-39:68-100',
    'b2.cell': '1,13',
    'b2.ink': '3-11:19-255 12-12:89-210 13-13:56-209 16-16:63-114 17-18:37-111 19-19:20-130 20-20:19-131 '
              '21-21:19-129 22-22:19-130 23-23:19-131 24-24:20-130 25-25:126-126 26-26:122-125',
    'b3.cell': '17,29',
    'b3.ink': '12-12:21-180 13-13:21-175 14-14:20-175 15-15:20-185 16-18:19-186 19-20:18-186 21-21:135-135 '
              '25-25:19-52 26-27:21-58 28-28:19-59 29-32:19-58 33-33:19-59 34-34:50-50 35-35:46-49',
    'b4.cell': '33,45',
    'b4.ink': '22-23:21-153 24-24:20-153 25-25:20-162 26-27:19-163 28-28:19-159 29-29:18-163 30-30:18-162 '
              '35-35:19-99 36-37:19-105 38-38:19-106 39-42:19-105 43-43:19-106 44-44:97-97 45-45:93-96',
    'b5.cell': '13,24',
    'b5.ink': '8-10:7-171 11-11:7-180 12-13:7-181 14-14:7-177 15-15:7-181 16-16:7-180 17-17:21-49 18-18:17-137 '
              '21-21:169-178 22-23:169-180 24-24:169-181 25-28:169-180 29-29:169-181',
    'b6.cell': '-1,-1',
    'b6.ink': '8-8:13-87 9-9:13-92 10-10:12-92 11-11:12-106 12-14:11-107 15-15:10-107 16-16:10-106 17-17:21-21 '
              '18-18:20-25 21-21:50-52 22-23:50-50 24-24:50-68 25-25:50-69 26-26:50-67 27-27:50-68 28-28:50-69 '
              '29-29:50-68',
    'b7.cell': '3,14',
    'b7.ink': '5-5:19-137 6-12:21-149 13-13:21-147',
    'b8.cell': '13,25',
    'b8.ink': '8-8:31-181 9-10:30-181 11-11:30-184 12-15:30-185 16-16:31-185 21-21:77-137 22-24:77-138 '
              '25-25:77-137 26-29:77-138 31-31:90-95',
    'b9.cell': '3,15',
    'b9.ink': '0-1:19-85 2-2:19-95 3-3:19-96 4-4:19-94 5-5:19-95 6-6:19-96 7-7:20-95 12-14:38-78 15-15:20-113 '
              '16-16:19-114 17-17:20-114 18-18:21-114 19-19:19-114',
    'focus': '1',
    'b2.focus': '17,0,259,28',
}
WHAT = {
    'b0': "Passwords' first radio button (BS_TOP, two lines)",
    'b1': "Passwords' second radio button (BS_TOP, three lines)",
    'b2': "Passwords' check box (BS_TOP, two lines)",
    'b3': 'a check box centred in its height',
    'b4': 'a check box with BS_BOTTOM',
    'b5': 'a radio button with BS_RIGHT and BS_LEFTTEXT',
    'b6': 'a push button of two centred lines',
    'b7': 'a radio button without BS_MULTILINE, cut',
    'b8': 'a check box with BS_CENTER',
    'b9': 'a check box with more lines than room, overhanging',
}


def lines(ink):
    """The bands of rows with ink: lines of text."""
    rows = []
    for run in ink.split():
        span = run.split(':')[0]
        first, last = (int(v) for v in span.rsplit('-', 1)) if span.count('-') == 1 else (int(span), int(span))
        rows += range(first, last + 1)
    bands = 0
    for i, row in enumerate(rows):
        if i == 0 or row != rows[i - 1] + 1:
            bands += 1
    return bands


def run(binary, pristine, out, name, fixture, dpi=None, setting=False):
    folder = out / name
    folder.mkdir(parents=True)
    drive = folder / 'c'
    if drive.is_symlink():
        raise SystemExit(f'{drive} is a link')
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    journal = folder / 'c.registry'
    source = win98_c.journal(pristine)
    body = source.read_bytes() if source.is_file() else JOURNAL_SIGNATURE
    if dpi:
        settings = 'HKEY_LOCAL_MACHINE\\Config\\0001\\Display\\Settings'
        for value in ('DPILogicalX', 'DPILogicalY', 'DPIPhysicalX', 'DPIPhysicalY'):
            body += text_value(settings, value, str(dpi))
    if setting:
        body += text_value('HKEY_LOCAL_MACHINE\\Software\\NEO98', 'CheckBoxesFollowFontSize', '1')
    journal.write_bytes(body)
    shutil.copyfile(fixture, drive / 'BUTTONML.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(folder / 'settings'), NEO98_LOG_DIR=str(folder / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (folder / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\BUTTONML.EXE', '--quiet',
                        '--desktop', '640x480', '--system-tree', str(drive), '--drive-c', str(drive),
                        '--registry', str(journal), '--crash-report-dir', str(folder / 'reports')],
                       cwd=folder, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                       timeout=180)
    written = win98_c.path('C:\\BUTTONML.TXT', drive)
    got = {}
    if written:
        for line in written.read_text(errors='replace').splitlines():
            if '=' in line:
                key, value = line.split('=', 1)
                got[key] = value.strip()
    picture = win98_c.path('C:\\BUTTONML.BMP', drive)
    if picture:
        shutil.copyfile(picture, folder / 'buttons.bmp')
    shutil.rmtree(drive, ignore_errors=True)
    return got


def main(expected=None, pristine=None, original='Windows 98 SE'):
    expected = expected or EXPECTED
    pristine = pristine or win98_c.use_pristine()
    out = neo98.output_directory('button-multiline')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    fixture = out / 'BUTTONML.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/button_multiline_fixture.c'), '-ffreestanding',
                    '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-lgdi32', '-o', str(fixture)], check=True)
    got = run(binary, pristine, out, 'normal', fixture)
    checks = {'the fixture ran to its end': got.get('done') == '1'}
    for key, value in expected.items():
        what = WHAT.get(key.split('.')[0], key)
        same = got.get(key) == value
        label = f'{key}, {what}: as in {original}'
        checks[label if same else f'{label}\n      got      {got.get(key)}\n      expected {value}'] = same
    for index in (0, 1, 2, 3, 4, 5, 6, 8, 9):
        count = lines(got.get(f'b{index}.ink', ''))
        checks[f'b{index} shows {count} lines (more than one)'] = count > 1
    checks[f'b7, without BS_MULTILINE, shows one line: {lines(got.get("b7.ink", ""))}'] = \
        lines(got.get('b7.ink', '')) == 1
    # Neo98's larger cell: 144 dots per inch with CheckBoxesFollowFontSize.
    large = run(binary, pristine, out, 'large-cell', fixture, 144, True)
    checks['the fixture ran at 144 dots per inch with the setting'] = large.get('done') == '1'
    for index in (0, 1, 2, 3, 4, 8):
        top, bottom = (int(v) for v in large.get(f'b{index}.cell', '-1,-1').split(','))
        # The circle of a radio button leaves the last row of its cell.
        rows = 18 if index in (0, 1) else 20
        checks[f'b{index} at 144: a cell of 20 rows, {rows} drawn ({top}..{bottom})'] = bottom - top + 1 == rows
        ink = large.get(f'b{index}.ink', '')
        first = min((int(run.split(':')[1].split('-')[0]) for run in ink.split()), default=-1)
        checks[f'b{index} at 144: the caption starts after the cell and its gap ({first})'] = first > 20 + 2
        checks[f'b{index} at 144: {lines(ink)} lines'] = lines(ink) > 1
    for index in (0, 1, 2):
        top = int(large.get(f'b{index}.cell', '-1,-1').split(',')[0])
        ink = large.get(f'b{index}.ink', '')
        start = int(ink.split()[0].split(':')[0].split('-')[0]) if ink else -1
        checks[f'b{index} at 144, BS_TOP: the cell ({top}) and the first line ({start}) at the top'] = \
            0 <= top < 6 and 0 <= start < 8
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    print('    pictures (buttons.bmp) and logs in', out)
    print(f'button multiline host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
