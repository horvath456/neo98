#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""FilterKeys acts on the keys, on neo98-kms.

SPI_SETFILTERKEYS kept the settings of FilterKeys without acting on input.
tools/filter_keys_fixture.c turns FilterKeys on and keeps the characters it
gets; the input script of neo98-kms holds each key down and lets it go up
(keydown=VK, keyup=VK):

  bounce  BounceKeys of 1.5 s: A, A again 150 ms after it went up, B, and A
          2.4 s later give "aba"
  slow    SlowKeys of 1 s: A held 100 ms gives nothing, B held 2.5 s gives
          "b"

Before, every press gave its character ("aaba", "ab"). The times leave the
host a second either way, so a loaded machine does not change the answer.
Esc, held as long as SlowKeys asks, writes C:\\FILTER.TXT and ends the
fixture, and with it the host: no step quits at a fixed time.

    build/analysis-venv/bin/python tools/test_filter_keys_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
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
WORK = ROOT / 'build/filter-keys'


def press(at, key, held):
    return [f'{at:.2f}:keydown={key}', f'{at + held:.2f}:keyup={key}']


SESSIONS = {
    'bounce': [*press(1.0, '41', 0.05), *press(1.2, '41', 0.05), *press(1.4, '42', 0.05), *press(3.5, '41', 0.05),
               *press(4.0, '1b', 0.05)],
    'slow': [*press(1.0, '41', 0.1), *press(1.5, '42', 2.5), *press(4.5, '1b', 2.0)],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/filter_keys_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'FILTER.EXE')], check=True)
    typed, codes = {}, {}
    try:
        for mode, steps in SESSIONS.items():
            env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
                       NEO98_HOST_INPUT=';'.join(['0:wait=FilterTest', *steps]))
            for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
                env.pop(variable, None)
            result = win98_c.path('C:\\FILTER.TXT', drive)
            if result:
                result.unlink()
            run = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                  'C:\\FILTER.EXE', '--command', f'C:\\FILTER.EXE {mode}', '--time-limit', '90'],
                                 cwd=ROOT, env=env, capture_output=True, text=True, timeout=150)
            (WORK / f'{mode}.txt').write_text(run.stdout + run.stderr)
            codes[mode] = run.returncode
            result = win98_c.path('C:\\FILTER.TXT', drive)
            typed[mode] = result.read_text() if result else ''
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    checks = {
        f'the fixture ended both runs itself, before the time limit: {codes}': all(c == 0 for c in codes.values()),
        f'bounce: a press 150 ms after the release is dropped: "{typed["bounce"]}"': typed['bounce'] == 'aba.',
        f'slow: a press of 100 ms is dropped, one of 2.5 s counts: "{typed["slow"]}"': typed['slow'] == 'b.',
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Logs in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'filter keys host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
