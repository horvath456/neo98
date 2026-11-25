#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The resource functions of USER take a module read as a data file.

LoadLibraryEx with LOAD_LIBRARY_AS_DATAFILE answers a view of the file with
bit 0 set, and the resource functions of KERNEL32 take it. So do those of
USER on Windows 98, which find their resources through KERNEL32 (LoadString
of USER32 is KERNEL32's own). tools/datafile_resource_fixture.c loads itself
that way and takes an icon, a cursor, a bitmap, a string, two menus and a
dialog through the handle; the resources are made here.

    build/analysis-venv/bin/python tools/test_datafile_resource_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc', 'i686-w64-mingw32-windres'] if not _shutil.which(_t)]
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
import win98_c  # noqa: E402


def image(width, height, bits, palette, pixel):
    """BITMAPINFOHEADER, palette and rows of a picture of 16 colours, all of
    one index, with an empty (opaque) mask behind it for an icon or a
    cursor."""
    header = struct.pack('<IiiHHIIiiII', 40, width, height * 2, 1, bits, 0, 0, 0, 0, len(palette), 0)
    colours = b''.join(bytes((b, g, r, 0)) for r, g, b in palette)
    row = (width * bits + 31) // 32 * 4
    line = bytes([pixel << 4 | pixel] * (width // 2))
    xor = (line + bytes(row - len(line))) * height
    mask = bytes((width + 31) // 32 * 4 * height)
    return header + colours + xor + mask


def icon():
    """A 32 by 32 icon of 16 colours, all red."""
    palette = [(0, 0, 0)] * 9 + [(255, 0, 0)] + [(0, 0, 0)] * 6
    body = image(32, 32, 4, palette, 9)
    return struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 32, 32, 16, 0, 1, 4, len(body), 22) + body


def cursor():
    """A 32 by 32 cursor of 16 colours, all blue, hot spot 5,7."""
    palette = [(0, 0, 0)] * 12 + [(0, 0, 255)] + [(0, 0, 0)] * 3
    body = image(32, 32, 4, palette, 12)
    return struct.pack('<HHH', 0, 2, 1) + struct.pack('<BBBBHHII', 32, 32, 16, 0, 5, 7, len(body), 22) + body


def bitmap():
    """An 8 by 4 bitmap of 24 bits, all green."""
    info = struct.pack('<IiiHHIIiiII', 40, 8, 4, 1, 24, 0, 0, 0, 0, 0, 0)
    rows = bytes((0, 255, 0) * 8) * 4
    return struct.pack('<2sIHHI', b'BM', 14 + len(info) + len(rows), 0, 0, 14 + len(info)) + info + rows


RESOURCES = '''
1 ICON "fixture.ico"
2 CURSOR "fixture.cur"
3 BITMAP "fixture.bmp"
STRINGTABLE
BEGIN
  101 "Read from the data file"
END
4 MENU
BEGIN
  MENUITEM "First", 200
  MENUITEM "Second", 201
END
NAMEDMENU MENU
BEGIN
  MENUITEM "Only", 202
END
5 DIALOG 10, 10, 160, 60
STYLE WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION "Data file dialog"
FONT 8, "MS Sans Serif"
BEGIN
  LTEXT "From the view", 100, 8, 8, 140, 10
END
'''

EXPECTED = {
    'datafile': '1',
    'find': '1',
    'loadimage-icon': '0000ff',
    'loadicon': '0000ff',
    'loadimage-cursor': 'ff0000 5,7',
    'loadcursor': 'ff0000',
    'loadimage-bitmap': '8x4 00ff00',
    'loadbitmap': '8x4 00ff00',
    'loadstring': '23 Read from the data file',
    'loadmenu': '2 First',
    'loadmenu-named': '1',
    'createdialog': 'Data file dialog 1',
    'dialogbox': '42',
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('datafile-resource')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    (out / 'fixture.ico').write_bytes(icon())
    (out / 'fixture.cur').write_bytes(cursor())
    (out / 'fixture.bmp').write_bytes(bitmap())
    (out / 'fixture.rc').write_text('#include <windows.h>\n' + RESOURCES)
    subprocess.run(['i686-w64-mingw32-windres', 'fixture.rc', '-O', 'coff', '-o', 'fixture.res.o'], cwd=out,
                   check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/datafile_resource_fixture.c'), str(out / 'fixture.res.o'),
                    '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lgdi32', '-lkernel32',
                    '-o', str(drive / 'DATAFILE.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\DATAFILE.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\DATAFILE.TXT', drive)
    lines = written.read_text(errors='replace').splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    got = dict(line.split('=', 1) for line in lines if '=' in line)
    failed = 0
    for name, expected in EXPECTED.items():
        okay = got.get(name) == expected
        failed += not okay
        print(f"{'ok  ' if okay else 'FAIL'} {name}: {got.get(name)!r}" + ('' if okay else f', wanted {expected!r}'))
    if failed:
        print('    log:', out / 'log.txt')
        return 1
    print('datafile resources: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
