#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""visible display colors after partial palette updates. Use serial.py."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_d3d_execute_texture_host import guard


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    os.chdir(ROOT)
    guard(8.6)
    out = ROOT / 'build/ddraw-partial-palette'
    out.mkdir(exist_ok=True)
    host = ROOT / 'build/native-app/neo98-host'
    print('host sha256:', hashlib.sha256(host.read_bytes()).hexdigest(), flush=True)
    subprocess.run(['i686-w64-mingw32-gcc', 'tools/ddraw_partial_palette_fixture.c',
                    '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-luser32', '-lkernel32', '-o', str(out / 'PALETTE.EXE')],
                   check=True, stdin=subprocess.DEVNULL)
    for engine in ('fex', 'unicorn'):
        guard()
        drive = out / ('c-' + engine)
        assert not drive.exists() and not drive.is_symlink()
        subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()) + '/', str(drive)],
                       check=True, stdin=subprocess.DEVNULL)
        process = None
        suffix = engine + ('-before' if args.baseline else '-after')
        shot = out / (suffix + '.ppm')
        shot.unlink(missing_ok=True)
        (out / 'PALETTE.BIN').unlink(missing_ok=True)
        try:
            assert not drive.is_symlink()
            env = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='15',
                       NEO98_HOST_INPUT=f'4:shot={shot};12:menu=quit', NEO98_HOST_MENU='',
                       NEO98_SETTINGS_DIR=str(out / ('settings-' + engine)),
                       NEO98_LOG_DIR=str(out / ('logs-' + engine)), NEO98_HOST_PASTEBOARD='unique')
            with (out / (suffix + '.log')).open('w') as log:
                process = subprocess.Popen([str(host), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                    '--exe', r'E:\PALETTE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive', 'E=' + str(out), '--no-registry-file', '--quiet', '--offline'],
                    env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=25)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=15)
                    raise
            rows = list(struct.iter_unpack('<2I', (out / 'PALETTE.BIN').read_bytes()))
            assert rows == [(n, 0) for n in range(1, 12)], rows
            assert process.returncode == 0
            with Image.open(shot) as frame:
                assert frame.width * 3 == frame.height * 4, frame.size
                colors = [frame.convert('RGB').getpixel(((1 + 2 * n) * frame.width // 8, frame.height // 2)) for n in range(4)]
            expected = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
            (out / (suffix + '.json')).write_text(json.dumps({'colors': colors, 'expected': expected, 'api_results': rows}) + '\n')
            if args.baseline:
                assert colors == [(255, 0, 0), (0, 0, 255), (255, 255, 0), (255, 255, 0)], colors
            else:
                assert colors == expected, colors
            print('PASS', suffix, colors, flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                shutil.rmtree(drive)


if __name__ == '__main__':
    main()
