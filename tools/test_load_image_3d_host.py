#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""LoadImage of a 16-colour bitmap with LR_LOADTRANSPARENT and LR_LOADMAP3DCOLORS.

tools/load_image_3d_fixture.c writes a bitmap of 4 by 2 pixels and loads it
four times: plain, with LR_LOADTRANSPARENT (the entry of the first pixel
becomes COLOR_WINDOW), with both flags (it becomes COLOR_3DFACE, as the
background of System Properties' OEM logo) and with LR_LOADMAP3DCOLORS alone
(the greys become the 3-D colours). Each engine runs on a private copy of
drive C:.

    build/analysis-venv/bin/python tools/test_load_image_3d_host.py [--binary HOST]
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402
import native_app  # noqa: E402
import win98_c  # noqa: E402

CHECKS = 20


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/load-image-3d-host')
    parser.add_argument('--engine', action='append', choices=native_app.ENGINE_NAMES)
    args = parser.parse_args()
    out = args.out.resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    exe = out / 'PROBE.EXE'
    environment = neo98.tool_environment()
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib',
                    '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp', '-o', str(exe),
                    str(ROOT / 'tools/load_image_3d_fixture.c'), '-luser32', '-lgdi32', '-lkernel32'],
                   env=environment, check=True, stdin=subprocess.DEVNULL)
    failed = []
    for engine in args.engine or native_app.test_engines():
        drive = out / ('c-' + engine)
        try:
            subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                            str(win98_c.use_pristine()), str(drive)], check=True)
            env = dict(environment, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', NEO98_CPU=engine,
                       NEO98_HOST_INPUT='', NEO98_HOST_MENU='', NEO98_HOST_DEADLINE='40',
                       NEO98_SETTINGS_DIR=str(out / ('settings-' + engine)),
                       NEO98_LOG_DIR=str(out / ('logs-' + engine)))
            with (out / (engine + '.log')).open('w') as log:
                subprocess.run([str(args.binary.resolve()), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                                '--exe', r'E:\PROBE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                                '--drive-readonly', 'E=' + str(out), '--no-registry-file', '--quiet', '--offline'],
                               env=env, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                               timeout=120)
            result = drive / 'LOADIMG.BIN'
            blob = result.read_bytes() if result.is_file() else b''
        finally:
            shutil.rmtree(drive, ignore_errors=True)
        if len(blob) < 4:
            print(f'FAIL {engine}: the program wrote no results; log {out / (engine + ".log")}')
            failed.append(engine)
            continue
        code, = struct.unpack_from('<I', blob)
        rows = list(struct.iter_unpack('<3I', blob[4:]))
        wrong = [(number, f'{got:06x}', f'{want:06x}') for number, got, want in rows if got != want]
        passed = code == 0 and len(rows) == CHECKS and not wrong
        print(('ok  ' if passed else 'FAIL'), engine, f'{len(rows) - len(wrong)} of {len(rows)} checks',
              wrong, flush=True)
        if not passed:
            failed.append(engine)
    if failed:
        raise SystemExit('load image 3d host failed: ' + ', '.join(failed))
    print('load image 3d host: all engines passed')


if __name__ == '__main__':
    main()
