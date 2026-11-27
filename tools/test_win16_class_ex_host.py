#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""RegisterClassEx and GetClassInfoEx of a 16-bit program (USER.397, USER.398).

The WNDCLASSEX of 16-bit USER starts with two dwords, cbSize and style; the
procedure follows at byte 8 and the small icon behind the class name, at
byte 32. Neo98 read the record as a WNDCLASS that began behind cbSize, so
the procedure was the high word of the style and the offset of the real
procedure, and the class name was read from the wrong place: the class was
not registered, and a program that registers its main class this way (the
Character Map of Windows 95) ended before it made a window.

tools/class_ex_win16_fixture.asm registers a class with RegisterClassEx,
creates a window of it, uses its window bytes, and reads the class back
with GetClassWord and GetClassInfoEx; it writes what it got into
C:\\CLSEX16.TXT. GetClassInfo and GetClassInfoEx also answered the low
word of the native cursor and icon, which no call of the 16-bit side knows;
they answer the words LoadCursor and LoadIcon gave now, and the small icon
RegisterClassEx kept instead of none.

    build/analysis-venv/bin/python tools/test_win16_class_ex_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['nasm'] if not _shutil.which(_t)]
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
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('win16-class-ex')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    win16_fixture.assemble(ROOT / 'tools/class_ex_win16_fixture.asm', out / 'CLSEX16.EXE', 'CLSEX16',
                           neo98.tool_environment())
    shutil.copyfile(out / 'CLSEX16.EXE', drive / 'CLSEX16.EXE')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\CLSEX16.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\CLSEX16.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    complete = len(data) == 20 + 34
    (atom, window, nccreates, creates, window_word, class_style, class_bytes, info_result, loaded_cursor,
     loaded_small) = struct.unpack_from('<10H', data) if complete else (0,) * 10
    info = data[20:] if complete else b'\0' * 34
    size, style = struct.unpack_from('<II', info)
    procedure_offset, procedure_segment, class_extra, window_extra = struct.unpack_from('<4H', info, 8)
    cursor, background = struct.unpack_from('<HH', info, 20)
    small_icon = struct.unpack_from('<H', info, 32)[0]
    checks = {
        'the program wrote its results': complete,
        f'RegisterClassEx answers an atom: {atom:#06x}': atom >= 0xc000,
        f'a window of the class exists: {window:#06x}': window != 0,
        f'its procedure got WM_NCCREATE and WM_CREATE: {nccreates}, {creates}': (nccreates, creates) == (1, 1),
        f'the window has its six window bytes: {window_word:#06x}': window_word == 0x5a17,
        f'GetClassWord answers the style: {class_style:#06x}': class_style == 0x000b,
        f'GetClassWord answers the class bytes: {class_bytes}': class_bytes == 4,
        f'GetClassInfoEx finds the class: {info_result}': info_result != 0,
        f'its style is the dword of WNDCLASSEX: {style:#010x}': style == 0x0000000b,
        f'its procedure is a far pointer of the program: {procedure_segment:04x}:{procedure_offset:04x}':
            procedure_segment != 0 and procedure_offset != 0,
        f'its class and window bytes: {class_extra}, {window_extra}': (class_extra, window_extra) == (4, 6),
        f'its cursor is the one LoadCursor gave: {cursor:#06x}, {loaded_cursor:#06x}':
            cursor != 0 and cursor == loaded_cursor,
        f'its background: {background}': background == 6,
        f'its small icon is the one LoadIcon gave: {small_icon:#06x}, {loaded_small:#06x}':
            small_icon != 0 and small_icon == loaded_small,
        f'cbSize is left as the program set it: {size:#x}': size == 0xeeeeeeee,
    }
    (out / 'results.json').write_text(json.dumps({'written': data.hex(), 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    written:', data.hex(), '\n    log:', out / 'log.txt')
    print(f'win16 class ex host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
