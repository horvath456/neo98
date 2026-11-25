#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The accessibility settings of SystemParametersInfo.

SPI_GET/SET FILTERKEYS, TOGGLEKEYS, MOUSEKEYS, STICKYKEYS, ACCESSTIMEOUT,
SERIALKEYS, SOUNDSENTRY, HIGHCONTRAST, SHOWSOUNDS, KEYBOARDPREF and
SCREENREADER answered ERROR_CALL_NOT_IMPLEMENTED, and Accessibility Options
of the control panel showed every feature greyed. Windows 98 keeps them
under HKEY_CURRENT_USER\\Control Panel\\Accessibility, one string value per
flag, and sends WM_SETTINGCHANGE for SPIF_SENDCHANGE.

tools/accessibility_fixture.c and Accessibility Options, on neo98-kms over one
private copy of drive C: with the picture of a 1024x768 desktop in memory:

  get      every structure answers with SKF_AVAILABLE and the like and the
           values of the registry Setup left; a wrong cbSize is refused
  set      StickyKeys on sends WM_SETTINGCHANGE with its action; ShowSounds is
           the metric SM_SHOWSOUNDS; High Contrast on makes the window colour
           that of "High Contrast Black", off brings the colour back
  again    a second start reads StickyKeys, ShowSounds and the keyboard
           preference back from the registry
  panel    Accessibility Options offers StickyKeys, FilterKeys and ToggleKeys

The fixture ends the host when it has written its answers, and the picture
of the panel waits until the machine is quiet: no step waits for a time.

    build/analysis-venv/bin/python tools/test_accessibility_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
    print('SKIP: needs i686-w64-mingw32-gcc')
    _sys.exit(77)
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/accessibility'
# "Use StickyKeys", "Use FilterKeys" and "Use ToggleKeys" on the page Keyboard.
LABELS = {'StickyKeys': (155, 177, 232, 190), 'FilterKeys': (155, 273, 225, 286),
          'ToggleKeys': (155, 369, 236, 383)}
BLACK, GREY = (0, 0, 0), (128, 128, 128)


def count(picture, box, colour):
    width, _, data = picture
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3]) == colour)


def session(kms, drive, program, command, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    line = [str(kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', program,
            '--time-limit', str(limit)]
    if command:
        line += ['--command', command]
    result = subprocess.run(line, cwd=ROOT, env=env, capture_output=True, text=True, timeout=limit + 90)
    return result.stdout + result.stderr + f'\nNEO98 TEST host ended with {result.returncode}\n'


def answers(drive, name):
    written = win98_c.path(name, drive)
    data = written.read_bytes() if written else b''
    return struct.unpack('<32i', data[:128]) if len(data) >= 128 else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/accessibility_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lgdi32', '-luser32', '-lkernel32',
                    '-o', str(drive / 'ACCESS.EXE')], check=True)
    try:
        # The panel first, as Setup left the settings, then the fixture twice.
        log = session(args.kms, drive, 'C:\\WINDOWS\\RUNDLL32.EXE', 'rundll32.exe shell32.dll,Control_RunDLL access.cpl',
                      ['0:wait=#32770', '0:idle=500', '0:shot={out}/panel.png', '0.5:quit=1'], 90)
        log += session(args.kms, drive, 'C:\\ACCESS.EXE', None, [], 90)
        first = answers(drive, 'C:\\ACCESS.TXT')
        log += session(args.kms, drive, 'C:\\ACCESS.EXE', 'ACCESS.EXE again', [], 90)
        second = answers(drive, 'C:\\ACCESS2.TXT')
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    r = first or (0,) * 32
    names = ('FILTERKEYS', 'TOGGLEKEYS', 'MOUSEKEYS', 'STICKYKEYS', 'ACCESSTIMEOUT', 'SERIALKEYS', 'SOUNDSENTRY',
             'HIGHCONTRAST')
    flags = dict(zip(names, r[1:9]))
    ends = re.findall(r'^NEO98 TEST host ended with (-?\d+)$', log, re.M)
    checks = {
        'the fixture ran twice without a stop': first is not None and second is not None and not stops,
        f'the panel was quiet for its picture, the fixture ended its runs itself: {ends}':
            'NEO98 KMS input idle=500 reached' in log and ends == ['0', '0', '0'],
        f'get: every structure answers and its feature is available: {flags}':
            all(value >= 0 for value in flags.values()) and
            all(flags[name] & 2 for name in names if name != 'ACCESSTIMEOUT'),
        f'get: the values Setup left: wait {r[9]}, repeat {r[10]}, time-out {r[11]}, mouse speed {r[12]}':
            (r[9], r[10], r[11], r[12]) == (1000, 500, 300000, 40),
        f'get: the serial port and the High Contrast scheme are named: {chr(r[13] & 0xff)!r} {chr(r[14] & 0xff)!r}':
            r[13] == ord('C') and r[14] == ord('H'),
        f'get: a STICKYKEYS of another cbSize is refused: {r[15]}': r[15] == 0,
        f'get: ShowSounds, SM_SHOWSOUNDS and the keyboard preference are off: {r[16:19]}': r[16:19] == (0, 0, 0),
        f'set: StickyKeys on sends WM_SETTINGCHANGE with SPI_SETSTICKYKEYS: {r[19]} {r[20]:#x}':
            r[19] == 1 and r[20] == 0x3b,
        f'set: ShowSounds on is SM_SHOWSOUNDS: {r[21]} {r[22]}': (r[21], r[22]) == (1, 1),
        f'set: High Contrast on paints windows black: {r[24]:06x} -> {r[26]:06x}, text {r[27]:06x}':
            r[25] == 1 and r[26] == 0 and r[27] == 0xffffff and r[24] != 0,
        f'set: High Contrast off brings the window colour back: {r[29]:06x}': r[28] == 1 and r[29] == r[24],
        f'set: every change was announced: {r[30]}': r[30] >= 4,
    }
    if second:
        checks[f'again: StickyKeys, ShowSounds and the keyboard preference are kept: '
               f'{second[4]:#x} {second[16:19]}'] = bool(second[4] & 1) and second[16:19] == (1, 1, 1)
    panel = read_png(WORK / 'panel.png') if (WORK / 'panel.png').is_file() else None
    for name, box in LABELS.items():
        black = count(panel, box, BLACK) if panel else 0
        grey = count(panel, box, GREY) if panel else -1
        checks[f'panel: "Use {name}" can be chosen: {black} black, {grey} grey'] = black > 10 and grey == 0
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    if failed:
        print('    first:', first, '\n    second:', second)
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'accessibility host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
