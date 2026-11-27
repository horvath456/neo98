#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Device bitmaps take the memory the host has, and the next one fails in order.

The bits of all device bitmaps of a session together had a fixed bound of
128 MiB of host memory, on every host. A bitmap larger than 64 KiB costs the
host twice (the block of the original GDI code and the copy the renderer
draws in), so 64 MiB of pixels fitted: sixteen bitmaps of 1024 x 1024 at 32
bits a pixel, or seventeen windows of 720 x 1280 kept as pictures. Windows 98
has no such number; there the bits are global memory and the machine's
memory is the bound. The bound now follows the host: a quarter of its
memory, between 128 MiB and 2 GiB, or the megabytes of NEO98_BITMAP_MEMORY.

tools/bitmap_memory_fixture.c on neo98-kms over a private copy of drive C:
creates compatible bitmaps of 1024 x 1024 until one fails, checks that an
earlier one still takes a pixel, deletes them all and creates them again:

  with a bound of 64 MiB: 7 bitmaps, then NULL with ERROR_NOT_ENOUGH_MEMORY
  (the process goes on), and after deleting them 7 again
  with a bound of 320 MiB: 39 of them (15 before, whatever the bound)
  with the bound of the host (it needs 1 GiB of memory): all 24 asked for

    build/analysis-venv/bin/python tools/test_bitmap_memory_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/bitmap-memory'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/bitmap_memory_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-lgdi32', '-luser32', '-lkernel32',
                    '-o', str(drive / 'BMPMEM.EXE')], check=True)
    logs = []

    def session(count, megabytes):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT', 'NEO98_BITMAP_MEMORY'):
            env.pop(variable, None)
        if megabytes:
            env['NEO98_BITMAP_MEMORY'] = str(megabytes)
        written = win98_c.path('C:\\BMPMEM.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\BMPMEM.EXE', '--command', f'BMPMEM.EXE {count}',
                                 '--time-limit', '240'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=300)
        logs.append(f'---- {count} {megabytes}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\BMPMEM.TXT', drive)
        found = {'exit': result.returncode}
        for line in (written.read_text().splitlines() if written else ()):
            name, _, value = line.partition('=')
            found[name] = int(value)
        return found

    host = os.sysconf('SC_PHYS_PAGES') * os.sysconf('SC_PAGESIZE')
    try:
        small, large = session(200, 64), session(200, 320)
        own = session(24, 0) if host >= 1 << 30 else None
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    stops = [line for line in '\n'.join(logs).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    checks = {
        f'a bitmap of 1024 x 1024 has 4 MiB of bits: {small.get("bytes each")}': small.get('bytes each') == 4 << 20,
        f'with a bound of 64 MiB, 7 of them fit: {small.get("created")}': small.get('created') == 7,
        f'the next one fails with ERROR_NOT_ENOUGH_MEMORY and the process goes on: error {small.get("error")}, '
        f'exit {small.get("exit")}, done {small.get("done")}':
            small.get('error') == 8 and small.get('exit') == 0 and small.get('done') == 1,
        f'a bitmap created before the failure takes a pixel and gives it back: {small.get("pixel")}':
            small.get('pixel') == 1,
        f'deleted, they make room for as many again: {small.get("created again")}':
            small.get('created again') == small.get('created'),
        f'with a bound of 320 MiB, 39 fit, and again after deleting them: {large.get("created")}, '
        f'{large.get("created again")}, error {large.get("error")}':
            large.get('created') == 39 and large.get('created again') == 39 and large.get('error') == 8,
        f'no process stopped at something missing: {stops[:1]}': not stops,
    }
    if own is not None:
        checks[f'with the bound of this host ({host >> 30} GiB of memory), all 24 asked for: {own.get("created")}, '
               f'{own.get("created again")}'] = own.get('created') == 24 and own.get('created again') == 24
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'bitmap memory host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
