#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The font size of the display: "Small Fonts", "Large Fonts" and a custom size.

Windows keeps the font size as dots per inch under
HKEY_CURRENT_CONFIG\\Display\\Settings (DPILogicalX, DPILogicalY, DPIPhysicalX,
DPIPhysicalY) together with the system fonts of the size, and the raster
fonts of the size under Display\\Fonts. GDI reads them when the display driver
starts and USER sizes the parts of a window by them: its sizes are stored in
twips and its fonts in points (docs/FONT-SIZE.md). The port answered 96 dots
per inch whatever the registry said.

tools/font_size_fixture.c stores a size the way the Settings page of Display
Properties does and, at the next start, writes down everything that follows
it. The numbers wanted here are those of Windows 98: the sizes are
MulDiv(twips, dots per inch, 1440) of what Setup wrote (CaptionHeight -270,
ScrollWidth -240, SmCaptionWidth -195, SmCaptionHeight -225, IconSpacing
-1125), the font heights -MulDiv(points, dots per inch, 72), and the cells
those of the raster fonts of the installation: SSERIFE.FON and VGASYS.FON
with "Small Fonts", SSERIFF.FON and 8514SYS.FON with "Large Fonts" and with
every custom size above 108.

  96    an untouched installation: 8 points are 11 pixels in a cell of 13,
        dialog units 6 by 13 (8 by 16 for the System font), caption 18
  120   "Large Fonts": 13 pixels in a cell of 16, dialog units 8 by 16 (10 by
        20), caption 23, scroll bars 20, a dialog of 200 by 100 units is 400
        by 200 pixels
  144   a custom size: the fonts of 120 dots per inch stay, 8 points take
        their 10 point size (cell 20), dialog units 10 by 20; 24 points, 48
        pixels, are the 14 point size twice as large
  back  96 again restores every number of the first run

    build/analysis-venv/bin/python tools/test_font_size_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
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

# What each size has to answer; a name that is missing here is only compared
# between the first and the last run.
WANT = {
    '96': {
        'LOGPIXELSX': 96, 'LOGPIXELSY': 96, 'HORZSIZE': 211, 'VERTSIZE': 158, 'dmLogPixels': 96,
        'MM_LOMETRIC.inch.x': 96, 'MM_LOMETRIC.inch.y': 96, 'MM_TWIPS.inch.x': 96,
        'iBorderWidth': 1, 'iScrollWidth': 16, 'iScrollHeight': 16, 'iCaptionWidth': 18, 'iCaptionHeight': 18,
        'iSmCaptionWidth': 13, 'iSmCaptionHeight': 15, 'iMenuWidth': 18, 'iMenuHeight': 18,
        'SM_CXVSCROLL': 16, 'SM_CYCAPTION': 19, 'SM_CYSMCAPTION': 16, 'SM_CYMENU': 19, 'SM_CXSIZE': 18,
        'SM_CXMENUCHECK': 13, 'SM_CYMENUCHECK': 13, 'SM_CXICONSPACING': 75, 'SM_CYICONSPACING': 75,
        'iHorzSpacing': 75, 'iVertSpacing': 75,
        'lfCaptionFont.lfHeight': -11, 'lfCaptionFont.lfWeight': 700, 'lfCaptionFont.height': 13,
        'lfSmCaptionFont.height': 13, 'lfMenuFont.lfHeight': -11, 'lfMenuFont.height': 13,
        'lfStatusFont.lfHeight': -11, 'lfMessageFont.lfHeight': -11, 'lfMessageFont.height': 13,
        'lfIconFont.lfHeight': -8, 'lfIconFont.height': 13,
        'SYSTEM_FONT.height': 16, 'SYSTEM_FIXED_FONT.height': 15, 'OEM_FIXED_FONT.height': 12,
        'ANSI_FIXED_FONT.height': 13, 'ANSI_VAR_FONT.height': 13, 'ANSI_VAR_FONT.lfHeight': 12,
        'DEFAULT_GUI_FONT.height': 13, 'DEFAULT_GUI_FONT.lfHeight': -11,
        'sans8.lfHeight': -11, 'sans8.height': 13, 'sans10.height': 16, 'sans24.height': 37,
        'arial10.lfHeight': -13, 'courier10.height': 13,
        'GetDialogBaseUnits.x': 8, 'GetDialogBaseUnits.y': 16, 'dialog.unit.x': 6, 'dialog.unit.y': 13,
        'dialog.width': 300, 'dialog.height': 163, 'dialog.button.width': 75, 'dialog.button.height': 23,
    },
    '120': {
        'LOGPIXELSX': 120, 'LOGPIXELSY': 120, 'HORZSIZE': 169, 'VERTSIZE': 127, 'dmLogPixels': 120,
        'MM_LOMETRIC.inch.x': 120, 'MM_LOMETRIC.inch.y': 120, 'MM_TWIPS.inch.x': 120,
        'iBorderWidth': 1, 'iScrollWidth': 20, 'iScrollHeight': 20, 'iCaptionWidth': 23, 'iCaptionHeight': 23,
        'iSmCaptionWidth': 16, 'iSmCaptionHeight': 19, 'iMenuWidth': 23, 'iMenuHeight': 23,
        'SM_CXVSCROLL': 20, 'SM_CYCAPTION': 24, 'SM_CYSMCAPTION': 20, 'SM_CYMENU': 24, 'SM_CXSIZE': 23,
        'SM_CXMENUCHECK': 15, 'SM_CYMENUCHECK': 15, 'SM_CXICONSPACING': 94, 'SM_CYICONSPACING': 94,
        'iHorzSpacing': 94, 'iVertSpacing': 94,
        'lfCaptionFont.lfHeight': -13, 'lfCaptionFont.lfWeight': 700, 'lfCaptionFont.height': 16,
        'lfSmCaptionFont.height': 16, 'lfMenuFont.lfHeight': -13, 'lfMenuFont.height': 16,
        'lfStatusFont.lfHeight': -13, 'lfMessageFont.lfHeight': -13, 'lfMessageFont.height': 16,
        'lfIconFont.lfHeight': -10, 'lfIconFont.height': 16,
        'SYSTEM_FONT.height': 20, 'SYSTEM_FIXED_FONT.height': 20, 'OEM_FIXED_FONT.height': 20,
        'ANSI_FIXED_FONT.height': 16, 'ANSI_VAR_FONT.height': 16, 'ANSI_VAR_FONT.lfHeight': 12,
        'DEFAULT_GUI_FONT.height': 16, 'DEFAULT_GUI_FONT.lfHeight': -13,
        'sans8.lfHeight': -13, 'sans8.height': 16, 'sans10.height': 20, 'sans24.height': 46,
        'arial10.lfHeight': -17, 'courier10.height': 16,
        'GetDialogBaseUnits.x': 10, 'GetDialogBaseUnits.y': 20, 'dialog.unit.x': 8, 'dialog.unit.y': 16,
        'dialog.width': 400, 'dialog.height': 200, 'dialog.button.width': 100, 'dialog.button.height': 28,
    },
    '144': {
        'LOGPIXELSX': 144, 'LOGPIXELSY': 144, 'HORZSIZE': 141, 'VERTSIZE': 105, 'dmLogPixels': 144,
        'MM_LOMETRIC.inch.x': 144, 'MM_TWIPS.inch.x': 144,
        'iBorderWidth': 2, 'iScrollWidth': 24, 'iScrollHeight': 24, 'iCaptionWidth': 27, 'iCaptionHeight': 27,
        'iSmCaptionWidth': 20, 'iSmCaptionHeight': 23, 'iMenuWidth': 27, 'iMenuHeight': 27,
        'SM_CXVSCROLL': 24, 'SM_CYCAPTION': 28, 'SM_CYMENU': 28, 'SM_CXMENUCHECK': 19,
        'SM_CXICONSPACING': 113, 'SM_CYICONSPACING': 113,
        'lfCaptionFont.lfHeight': -16, 'lfCaptionFont.height': 20, 'lfSmCaptionFont.height': 16,
        'lfMenuFont.lfHeight': -16, 'lfMenuFont.height': 20, 'lfMessageFont.height': 20,
        'lfIconFont.lfHeight': -12, 'lfIconFont.height': 16,
        'SYSTEM_FONT.height': 20, 'SYSTEM_FIXED_FONT.height': 20, 'ANSI_VAR_FONT.height': 16,
        'DEFAULT_GUI_FONT.height': 20, 'DEFAULT_GUI_FONT.lfHeight': -16,
        'sans8.lfHeight': -16, 'sans8.height': 20, 'sans10.lfHeight': -20, 'sans10.height': 25,
        'sans24.lfHeight': -48, 'sans24.height': 60, 'arial10.lfHeight': -20, 'courier10.height': 20,
        'GetDialogBaseUnits.x': 10, 'GetDialogBaseUnits.y': 20, 'dialog.unit.x': 10, 'dialog.unit.y': 20,
        'dialog.width': 500, 'dialog.height': 250, 'dialog.button.width': 125, 'dialog.button.height': 35,
    },
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('font-size')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), out / 'c.registry')
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/font_size_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lgdi32', '-lkernel32', '-ladvapi32',
                    '-o', str(drive / 'FONTSIZE.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='40',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)

    def run(name, command):
        with (out / f'{name}.txt').open('wb') as log:
            subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\FONTSIZE.EXE',
                            '--command', f'C:\\FONTSIZE.EXE {command}', '--quiet', '--system-tree', str(drive),
                            '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                            '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                           stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)

    def read(name):
        result = win98_c.path('C:\\FONTSIZE.TXT', drive)
        if result:
            result.unlink()
        run(name, 'read')
        result = win98_c.path('C:\\FONTSIZE.TXT', drive)
        answers = {}
        for line in result.read_text().splitlines() if result else []:
            key, _, value = line.rpartition(' ')
            answers[key] = int(value)
        (out / f'{name}.values').write_text(''.join(f'{k} {v}\n' for k, v in answers.items()))
        return answers

    answers = {}
    try:
        answers['96'] = read('96')
        for size in ('120', '144'):
            run(f'set-{size}', f'set {size}')
            answers[size] = read(size)
        run('set-96', 'set 96')
        answers['back'] = read('back')
        boot = win98_c.path('C:\\WINDOWS\\SYSTEM.INI', drive)
        system_ini = boot.read_text(errors='replace') if boot else ''
    finally:
        shutil.rmtree(drive, ignore_errors=True)
    checks = {}
    titles = {'96': 'an untouched installation is "Small Fonts", 96 dots per inch',
              '120': '"Large Fonts", 120 dots per inch, after a restart',
              '144': 'a custom size of 144 dots per inch, after a restart'}
    for size, want in WANT.items():
        found = answers.get(size, {})
        wrong = {name: (found.get(name), value) for name, value in want.items() if found.get(name) != value}
        checks[f'{titles[size]} (found, wanted: {wrong})'] = bool(found) and not wrong
    first, last = answers.get('96', {}), answers.get('back', {})
    changed = {name: (value, last.get(name)) for name, value in first.items() if last.get(name) != value}
    checks[f'96 again restores every number: {changed}'] = bool(first) and not changed
    checks['the change wrote the system fonts of 96 back to SYSTEM.INI [boot]'] = \
        'fonts.fon=vgasys.fon' in system_ini.lower() and 'aspect=100,96,96' in system_ini.lower()
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    print(f'font size host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed; values in {out}')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
