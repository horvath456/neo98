#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DrawTextEx: the end ellipsis of a single line with its prefix.

COMCTL32 draws a tab label with "&" ("Date && Time") through DrawTextEx with
DT_END_ELLIPSIS and without DT_NOPREFIX. The port refused that combination
with ERROR_NOT_SUPPORTED and drew nothing. tools/draw_text_ellipsis_fixture.c
draws such a label into a memory DC:

  wide    a label that fits keeps its prefix and draws exactly as without the
          ellipsis
  narrow  a label that does not fit draws within its rectangle, shortened as
          the plain text with DT_NOPREFIX is

    build/analysis-venv/bin/python tools/test_draw_text_ellipsis_host.py [--no-build]
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
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('draw-text-ellipsis')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/draw_text_ellipsis_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lgdi32', '-luser32', '-lkernel32',
                    '-o', str(drive / 'DTEXT.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='40',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\DTEXT.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\DTEXT.TXT', drive)
    data = written.read_bytes() if written else b''
    shutil.rmtree(drive, ignore_errors=True)
    if len(data) < 32:
        print('FAIL the program wrote its results', len(data), 'bytes\n    log:', out / 'log.txt')
        return 1
    r = struct.unpack('<8i', data[:32])
    checks = {
        f'wide: DrawTextEx with the ellipsis and the prefix draws: {r[0]}': r[0] > 0,
        'wide: it draws exactly as without the ellipsis': r[1] > 0 and r[4] == 1,
        f'narrow: the label that does not fit draws: {r[2]}, {r[5]} dark pixels': r[2] > 0 and r[5] > 20,
        f'narrow: it stays within its 40 pixels: rightmost {r[6]}': r[6] < 40,
        'narrow: it is shortened as the plain text with DT_NOPREFIX': r[3] > 0 and r[7] == 1,
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    print(f'draw text ellipsis host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
