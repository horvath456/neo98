#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""original DDRAW v1 mode selection, indexed flip chain and scanout.

Run through a shared serial test lock with an already built host.
"""
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
import shutil
import subprocess
from PIL import Image
from ui_clipboard_test_support import ROOT, machine, host


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/ddraw-640400'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'MODE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/ddraw_640400_fixture.c'),
                    '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-lddraw', '-luser32', '-lkernel32', '-o', str(exe)], check=True)
    results = {'host_sha256': hashlib.sha256((ROOT / 'build/native-app/neo98-host').read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'tests': {}}
    for cpu in ('fex', 'unicorn'):
        if shutil.disk_usage(ROOT).free < 8 * 1024**3:
            raise RuntimeError('STOP: fewer than 8 GiB free')
        os.environ['NEO98_CPU'] = cpu
        folder = out / (cpu + ('-before' if args.baseline else '-after'))
        picture = folder / 'frame.ppm'
        try:
            with machine(folder) as drive:
                log = host(folder, drive, r'E:\MODE.EXE', seconds=8,
                           steps=f'3:shot={picture}', extra=('--drive-readonly', 'E=' + str(out)))
                expected = 100 if args.baseline else 0
                assert f'NEO98 HOST exited code={expected} ' in log, log[-3000:]
                if not args.baseline:
                    im = Image.open(picture).convert('RGB')
                    for x, y, color in ((1, 1, (255, 0, 0)), (3, 1, (0, 255, 0)),
                                        (1, 3, (0, 0, 255)), (3, 3, (255, 255, 0))):
                        assert im.getpixel((im.width * x // 4, im.height * y // 4)) == color
                    im.save(folder / 'frame.png')
        finally:
            picture.unlink(missing_ok=True)
            (folder / 'c.registry').unlink(missing_ok=True)
        results['tests'][cpu] = 'expected mode rejection' if args.baseline else 'PASS'
        print(cpu, results['tests'][cpu], flush=True)
    (out / ('before.json' if args.baseline else 'results.json')).write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
