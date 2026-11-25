#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""What the 16-bit parts of the display properties ask of USER and the BIOS.

tools/settings_win16_fixture.asm calls what the 16-bit pages of the display
properties (DESKCP16.DLL) and other 16-bit settings pages use:

  temp   USER.661 SetSysColorsTemp takes 25 colours and 25 brushes of 16
         bits and answers a cookie that puts the old colours back; USER.662
         DrawMenuBarTemp draws a menu into a rectangle of 16 bits and answers
         the height of the bar; INT 10h AX=4F10h (VBE power management)
         answers 014Fh, function known and failed, as a display without it
  mode   LoadLibrary of FRAMEBUF.DRV, the driver of a mode the Settings page
         loads for its ValidateMode, answers the loaded DISPLAY, which is the
         port's own; its ValidateMode answers 0 for the mode of the screen,
         2 for a resolution and 3 for a depth the display lacks
  spi    USER.483 SystemParametersInfo fills the structures of 16 bits, ints
         as words and LOGFONTs of 50 bytes: the icon title font, the
         NONCLIENTMETRICS of 10Eh bytes and the ICONMETRICS of 3Ah bytes,
         and nothing behind them; SPI_GETBORDER writes one word

The program writes its words again after each part, so a part that stops the
run leaves the ones before it; the log line of the stop is printed.

    build/analysis-venv/bin/python tools/test_win16_settings_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import os
import shutil
import string
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

WORDS = 32


def letter(word):
    """Whether the low byte of a word is an ASCII letter, the first of a face name."""
    return chr(word & 0xff) in string.ascii_letters


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-settings')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/settings_win16_fixture.asm', out / 'SET16.EXE', 'SET16',
                           neo98.tool_environment(), ('KERNEL', 'USER', 'GDI'))
    shutil.copyfile(out / 'SET16.EXE', drive / 'SET16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\SET16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\SET16OUT.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    # A part that stopped the run left no words: they read as FFFFh.
    w = struct.unpack_from(f'<{WORDS}H', data[:WORDS * 2].ljust(WORDS * 2, b'\xff'))
    checks = {
        'temp: SetSysColorsTemp answers a cookie': w[0] == 1,
        f'temp: the menu has the colour of the temporary scheme: {w[2]:04x}{w[1]:04x}': (w[1], w[2]) == (0x3456, 0x12),
        f'temp: DrawMenuBarTemp answers the height of the bar: {w[3]}': 10 <= w[3] < 40,
        'temp: the cookie puts the old colours back': w[4] == 1 and w[5] == 0,
        f'temp: INT 10h AX=4F10h answers 014Fh: {w[6]:04x}': w[6] == 0x014f,
        'mode: LoadLibrary of FRAMEBUF.DRV answers a module': w[7] == 1,
        'mode: it is the loaded DISPLAY': w[8] == 0,
        'mode: GetProcAddress finds ValidateMode': w[9] == 1,
        f'mode: ValidateMode answers 0, 2 and 3: {w[10:13]}': w[10:13] == (0, 2, 3),
        f'spi: the icon title LOGFONT of 16 bits: height {w[14]:04x}, face {chr(w[15] & 0xff)!r}':
            w[13] == 1 and 0xff80 <= w[14] <= 0xffff and letter(w[15]),
        'spi: nothing written behind that LOGFONT': w[16] == 0xaaaa,
        f'spi: NONCLIENTMETRICS of 16 bits, border {w[18]}, scroll bar {w[19]}':
            w[17] == 1 and w[18] == 1 and 12 <= w[19] < 40,
        f'spi: its caption and message fonts have their faces: {chr(w[20] & 0xff)!r} {chr(w[21] & 0xff)!r}':
            letter(w[20]) and letter(w[21]),
        'spi: nothing written behind the NONCLIENTMETRICS': w[22] == 0xaaaa,
        f'spi: ICONMETRICS of 16 bits, spacing {w[24]}, face {chr(w[25] & 0xff)!r}':
            w[23] == 1 and 32 <= w[24] < 200 and letter(w[25]),
        'spi: nothing written behind the ICONMETRICS': w[26] == 0xaaaa,
        f'spi: SPI_GETBORDER writes one word: {w[28]}, behind it {w[29]:04x}':
            w[27] == 1 and 1 <= w[28] < 16 and w[29] == 0xaaaa,
        'every call took its word count off the stack': w[31] == 0,
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        text = (out / 'log.txt').read_text(errors='replace').splitlines()
        stops = [line for line in text if 'not ported' in line or 'no native provider' in line]
        print('    words:', [hex(v) for v in w], '\n    stop:', stops[:1], '\n    log:', out / 'log.txt')
    print(f'win16 settings host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
