#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An owner, its owned window and the topmost band.

SetWindowPos with HWND_TOPMOST and HWND_NOTOPMOST, as Windows documents it:
when a non-topmost window is made topmost, its owned windows are made
topmost too and its owners are not changed; when a topmost window is made
non-topmost, its owners and its owned windows become non-topmost as well.
Owned windows stay above their owner. Neo98 moved the owner into the band
and left its owned window behind, below it.

tools/owned_topmost_fixture.c writes the WS_EX_TOPMOST bit and the place in
the order of a plain window P, an owner O and its owned popup W after each
step of three cases (see there).

    build/analysis-venv/bin/python tools/test_owned_topmost_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)

import json
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

# Step: (P topmost, P place, O topmost, O place, W topmost, W place).
EXPECTED = {
    'A made': (0, 2, 0, 1, 0, 0),
    'A owner topmost': (0, 2, 1, 1, 1, 0),      # W goes with its owner and stays above it
    'A plain to top': (0, 2, 1, 1, 1, 0),       # HWND_TOP keeps P below the band
    'A owned topmost': (0, 2, 1, 1, 1, 0),      # the owner stays in the band
    'B made': (0, 2, 0, 1, 0, 0),
    'B owned topmost': (0, 2, 0, 1, 1, 0),      # the owner is not changed
    'B plain to top': (0, 1, 0, 2, 1, 0),
    'C owner topmost': (0, 2, 1, 1, 1, 0),
    'C owned not topmost': (0, 2, 0, 1, 0, 0),  # owner and owned leave the band together
}


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('owned-topmost')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(pristine.resolve()), str(drive)], check=True)
    (drive / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/owned_topmost_fixture.c'), '-O1', '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32', '-o', str(drive / 'OWNTOP.EXE')],
                   env=neo98.tool_environment(), check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='20',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\OWNTOP.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=120)
    written = win98_c.path('C:\\OWNTOP.TXT', drive)
    lines = written.read_text().splitlines() if written else []
    shutil.rmtree(drive, ignore_errors=True)
    found = {}
    for line in lines:
        step, rest = line.split(' P ')
        words = rest.replace('O ', '').replace('W ', '').split()
        found[step] = tuple(int(word) for word in words)
    checks = {'the program wrote all steps': len(found) == len(EXPECTED)}
    for step, want in EXPECTED.items():
        checks[f'{step}: {found.get(step)}'] = found.get(step) == want
    (out / 'results.json').write_text(json.dumps({'lines': lines, 'checks': checks}, indent=1))
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    expected (P bit, place, O bit, place, W bit, place):', EXPECTED, '\n    log:', out / 'log.txt')
    print(f'owned topmost host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
