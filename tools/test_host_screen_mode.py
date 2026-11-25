#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The size that fills the Mac's screen in full screen is a mode like any other.

The host runs headless with NEO98_HOST_SCREEN=1470x956 and
NEO98_HOST_SAFE_TOP=33, a 13-inch MacBook Air with its camera housing. Out
of the seamless view the display driver offers 1470x923, the screen less the
housing. tools/host_screen_mode_fixture.c finds it among the modes and stores
it with CDS_UPDATEREGISTRY:
- the change succeeds, the screen and the host's canvas are 1470x923 and the
  registry holds Resolution 1470,923 (before, the size of the host's screen
  was never stored, and choosing it in Display Properties did nothing);
- in full screen the picture keeps clear of the housing: it starts 33 points
  or more below the top of the window;
- started again on the same C: and registry, the desktop is 1470x923 from the
  start.

    build/analysis-venv/bin/python tools/test_host_screen_mode.py [--no-build]

Results in build/host-screen-mode/results.json.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's


def run(binary, out, name, steps):
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_SCREEN='1470x956', NEO98_HOST_SAFE_TOP='33',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree', '--exe', 'C:\\HSMODE.EXE',
               '--quiet', '--drive-c', str(out / 'drive-c'), '--registry', str(out / 'registry'),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / f'{name}.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=600).returncode
    return code, log_path.read_text(errors='replace')


def main():
    out = neo98.output_directory('host-screen-mode')
    for name in ('drive-c', 'settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    (out / 'registry').unlink(missing_ok=True)
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(win98_c.FOLDER.resolve()), str(out / 'drive-c')], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/host_screen_mode_fixture.c'), '-O1', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(out / 'drive-c/HSMODE.EXE')],
                   check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    first_code, first = run(binary, out, 'host-1', ['0:wait=|offered 1 stored 0 1470x923', '0:idle=500',
                                                    '1:menu=fullscreen', '2:idle=500', '3:menu=quit'])
    registry = (out / 'registry').read_bytes() if (out / 'registry').exists() else b''
    second_code, second = run(binary, out, 'host-2', ['0:wait=|screen 1470x923', '0:idle=500', '1:menu=quit'])
    shutil.rmtree(out / 'drive-c', ignore_errors=True)
    view = re.search(r'^NEO98 HOST view scale=\d+ fullscreen=1 window=(\d+)x(\d+) guest=(-?\d+),(-?\d+),(\d+)x(\d+)$',
                     first, re.M)
    checks = {
        'the mode is offered and stored, the screen is 1470x923':
            'input wait=|offered 1 stored 0 1470x923 reached' in first,
        "the host's canvas follows": re.search(r'^NEO98 HOST canvas 1470x923 ', first, re.M) is not None,
        'the registry holds Resolution 1470,923': b'1470,923' in registry,
        'in full screen the picture starts below the camera housing': view is not None and int(view.group(4)) >= 33,
        'the first start ended': first_code == 0,
        'the restart has the desktop in 1470x923 from the start':
            'input wait=|screen 1470x923 reached' in second
            and re.search(r'^NEO98 HOST canvas 1470x923 ', second, re.M) is not None,
        'the restart ended': second_code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'HOST SCREEN MODE FAIL; see {out}')
        return 1
    print('HOST SCREEN MODE PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
