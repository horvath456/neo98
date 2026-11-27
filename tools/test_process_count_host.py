#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Sixty-four programs run at once, and one too many fails as on Windows.

The session kept its processes in tables of 20 entries. The start of a
twenty-first process got as far as the loader, which found no room for it
in a block of the shared arena and called the program a bad image:
CreateProcess failed with ERROR_BAD_EXE_FORMAT and WinExec answered 11.
Windows 98 has no such number; there a start fails when the memory runs out.

The tables hold 72 processes now, more than the 64 child processes the port
has records for, and a start beyond that fails before anything of the child
exists, with ERROR_NOT_ENOUGH_MEMORY.

tools/process_count_fixture.c on neo98-kms over a private copy of drive C:

  it starts itself 64 times; every child registers a window, counts itself
  in a shared section and waits; all 64 run at once and end with code 0
  asked for 70, it gets 64: the next CreateProcess answers
  ERROR_NOT_ENOUGH_MEMORY and WinExec 8, not 2 (file not found) or 11

    build/analysis-venv/bin/python tools/test_process_count_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/process-count'


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/process_count_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'PROCS.EXE')], check=True)
    logs = []

    def session(count):
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:640x480')
        for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
            env.pop(variable, None)
        written = win98_c.path('C:\\PROCS.TXT', drive)
        if written:
            written.unlink()
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '640x480',
                                 '--exe', 'C:\\PROCS.EXE', '--command', f'PROCS.EXE {count}',
                                 '--time-limit', '240'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=300)
        logs.append(f'---- {count}: {result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\PROCS.TXT', drive)
        found = {}
        for line in (written.read_text().splitlines() if written else ()):
            name, _, value = line.partition('=')
            found[name] = int(value)
        return found

    try:
        full, over = session(64), session(70)
    finally:
        (WORK / 'log.txt').write_text('\n'.join(logs))
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    stops = [line for line in '\n'.join(logs).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line
             or 'Session block has no room' in line]
    checks = {
        f'64 processes start, run at once and end with code 0: {full}':
            full.get('started') == 64 and full.get('running') == 64 and full.get('ended') == 64
            and full.get('first error') == 0,
        f'of 70 asked for, the ones the port has room for run: {over.get("started")}, {over.get("running")} at once':
            64 <= over.get('started', 0) < 70 and over.get('running') == over.get('started')
            and over.get('ended') == over.get('started'),
        f'the next CreateProcess fails with ERROR_NOT_ENOUGH_MEMORY: {over.get("first error")}':
            over.get('first error') == 8,
        f'WinExec answers the same, not "file not found" or "bad format": {over.get("WinExec")}':
            over.get('WinExec') == 8,
        f'no process stopped half started: {stops[:1]}': not stops,
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'process count host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
