#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""FilterKeys acts on the keys.

SPI_SETFILTERKEYS kept the settings of FilterKeys without acting on input.
tools/filter_keys_fixture.c turns FilterKeys on and keeps the characters it
gets; the keys are pressed and released by the input of neo98-host:

  bounce  BounceKeys of 500 ms: A, A again 150 ms after it went up, B, and A
          after a second give "aba"
  slow    SlowKeys of 300 ms: A held 100 ms gives nothing, B held 500 ms
          gives "b"

Before, every press gave its character ("aaba", "ab").

    build/analysis-venv/bin/python tools/test_filter_keys_host.py [--no-build]
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
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402


def press(at, key, held):
    return [f'{at:.2f}:keydown={key}', f'{at + held:.2f}:keyup={key}']


SESSIONS = {
    'bounce': [*press(1.0, '41', 0.05), *press(1.2, '41', 0.05), *press(1.4, '42', 0.05), *press(2.2, '41', 0.05),
               *press(2.6, '1b', 0.05), '3.5:menu=quit'],
    'slow': [*press(1.0, '41', 0.1), *press(1.5, '42', 0.5), *press(2.5, '1b', 0.5), '4:menu=quit'],
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('filter-keys')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/filter_keys_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'FILTER.EXE')], check=True)
    typed = {}
    try:
        for mode, steps in SESSIONS.items():
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='40',
                       NEO98_HOST_INPUT=';'.join(['0:wait=FilterTest', *steps]),
                       NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
            for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
                env.pop(key, None)
            result = win98_c.path('C:\\FILTER.TXT', drive)
            if result:
                result.unlink()
            with (out / f'{mode}.txt').open('wb') as log:
                subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\FILTER.EXE',
                                '--command', f'C:\\FILTER.EXE {mode}', '--quiet', '--system-tree', str(drive),
                                '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                                '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                               stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
            result = win98_c.path('C:\\FILTER.TXT', drive)
            typed[mode] = result.read_text() if result else ''
    finally:
        shutil.rmtree(drive, ignore_errors=True)
    checks = {
        f'bounce: a press 150 ms after the release is dropped: "{typed["bounce"]}"': typed['bounce'] == 'aba.',
        f'slow: a press of 100 ms is dropped, one of 500 ms counts: "{typed["slow"]}"': typed['slow'] == 'b.',
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    print(f'filter keys host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
