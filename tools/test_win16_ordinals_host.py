#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A sample of the gates of the pass over the whole export tables in the host.

tools/ordinals_win16_fixture.asm calls KERNEL, USER and GDI ordinals that no
program of the matrix had asked for : structures in
and out (RECT, POINT, SCROLLINFO, MENUITEMINFO), a POINT by value, a pair in
DX:AX, a far callback, the HDWP as a word and the constants read off USER.EXE.
The last word is the stack pointer after the last call against the one before
the first, which a wrong word count would move.

    build/analysis-venv/bin/python tools/test_win16_ordinals_host.py [--no-build]
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

WORDS = 59


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-ordinals')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine.resolve()), str(drive)], check=True)
    win16_fixture.assemble(ROOT / 'tools/ordinals_win16_fixture.asm', out / 'ORD16.EXE', 'ORD16',
                           neo98.tool_environment(), ('KERNEL', 'USER', 'GDI', 'SHELL', 'KEYBOARD'))
    shutil.copyfile(out / 'ORD16.EXE', drive / 'ORD16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\ORD16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\ORD16OUT.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    if len(data) < 128 + 4 + 16 + 32:
        print('FAIL the program wrote its results', len(data), 'bytes\n    log:', out / 'log.txt')
        return 1
    w = struct.unpack_from(f'<{WORDS}H', data)
    s = [v - 65536 if v & 0x8000 else v for v in w]
    text, menu, environment = data[128:132], data[132:148], data[148:180].split(b'\0')[0]
    checks = {
        'KERNEL.79 [AnsiUpper] answers the string and upper-cases it': s[0] == 0 and text == b'ABC\0',
        'USER.14 GetTimerResolution is 1000 in DX:AX': (w[1], w[2]) == (1000, 0),
        'USER.517 WNetAddConnection answers WN_NOT_SUPPORTED': w[3] == 1,
        'USER.76 PtInRect takes x from the low word of the point': (w[4], w[5]) == (1, 0),
        'USER.373 SubtractRect widens and narrows three RECTs': w[6] == 1 and s[7:11] == [0, 40, 100, 100],
        'USER.77 OffsetRect moves the far RECT by dx and dy': s[11:15] == [11, -3, 13, -1],
        'USER.78 InflateRect widens the far RECT by dx and dy': s[15:19] == [7, 12, 23, 18],
        'GDI.52 CreateCompatibleDC': w[19] == 1,
        'GDI.483 MoveToEx writes the previous POINT as two words': w[20] == 1 and w[21] == 1 and s[22:24] == [10, 20],
        'GDI.78 GetCurrentPosition answers y:x in DX:AX': s[24:26] == [30, -5],
        'GDI.63 CreatePolygonRgn and GDI.134 GetRgnBox': w[26] == 1 and w[27] == 2 and s[28:32] == [2, 3, 12, 8],
        'USER.41 CreateWindow of the window to enumerate': w[32] == 1,
        'USER.54 EnumWindows calls the far procedure with the window and the LONG':
            w[33] == 1 and w[34] == 1 and (w[35], w[36]) == (0x5678, 0x1234),
        'USER.475 SetScrollInfo and USER.476 GetScrollInfo': w[37] == 9 and w[38] == 1 and s[39:42] == [3, 70, 9],
        'USER.415 CreatePopupMenu, USER.441 InsertMenuItem, USER.443 GetMenuItemInfo':
            w[42] == 1 and w[43] == 1 and w[44] == 1 and (w[45], w[46]) == (0x123, 4) and menu[:5] == b'Item\0',
        'USER.259 to 261 position a window through an HDWP word':
            w[47] == 1 and w[48] == 1 and w[49] == 1 and s[50:54] == [5, 6, 35, 46],
        # TRUE in DX, the length with the terminating zero in AX, as SHELL32 counts.
        'SHELL.37 DoEnvironmentSubst expands through SHELL32':
            (w[54], w[55]) == (len(environment) + 1, 1) and environment == b'C:\\WINDOWS\\X',
        # Character Map names the keystroke of a character with it; a
        # capital letter is the letter's key with Shift (1 in the high byte).
        'KEYBOARD.129 VkKeyScan answers the key and the shift state': (w[56], w[57]) == (0x41, 0x141),
        'every gate took its word count off the stack': w[58] == 0,
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    words:', s, '\n    text:', text, menu, environment, '\n    log:', out / 'log.txt')
    print(f'win16 ordinals host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
