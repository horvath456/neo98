#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""In full screen the Mac's menu bar and Dock are hidden; Ctrl+Shift+M shows the menu bar.

A menu bar that hides by itself comes back whenever the pointer is in its
strip, and on a display with a camera housing that is the whole strip beside
the housing: a window at the top of the Windows desktop could not be clicked,
the pointer turned into the Mac's and only the menu bar answered. Now in full
screen both bars are hidden for good (HostMenu::FullScreenBars), and
Ctrl+Shift+M shows the menu bar and hides it again, as on Linux.

The host runs headless; tools/fullscreen_bars_fixture.c is a window at the
top of the screen that counts the M keys and the clicks it gets in its
title. The host prints each change of the bars ("NEO98 HOST bars menu=...
dock=..."):
- full screen hides the menu bar and the Dock;
- Ctrl+Shift+M shows the menu bar, the Dock stays hidden, and the window
  does not get the M;
- Ctrl+Shift+M again hides the menu bar; a plain M reaches the window;
- out of full screen both bars are back, and Ctrl+Shift+M is the window's.
Whether a click in the strip beside the housing reaches the window takes a
Mac with one in full screen; the window counts the clicks for that.

    build/analysis-venv/bin/python tools/test_fullscreen_bars_host.py [--no-build]

Results in build/fullscreen-bars-host/results.json.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if _sys.platform != 'darwin':  # the menu bar and the Dock are the Mac's
        print('SKIP: the menu bar and the Dock of full screen are the Mac\'s')
        _sys.exit(77)
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

# USB usages of the keys.
CTRL, SHIFT, M = 224, 225, 16
CHORD = [f'sdlkey={CTRL},1', f'sdlkey={SHIFT},1', f'sdlkey={M},1', f'sdlkey={M},0', f'sdlkey={SHIFT},0',
         f'sdlkey={CTRL},0']


def main():
    out = neo98.output_directory('fullscreen-bars-host')
    for name in ('drive-c', 'settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(out / 'drive-c')], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/fullscreen_bars_fixture.c'), '-O1', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(out / 'drive-c/FSBARS.EXE')],
                   check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    steps = ['0:wait=|keys 0 clicks 0', '0:idle=500', '1:menu=fullscreen', '2:idle=300',
             *(f'3:{s}' for s in CHORD), '3:idle=300',
             *(f'4:{s}' for s in CHORD), '4:idle=300',
             f'5:sdlkey={M},1', f'5:sdlkey={M},0', '5:wait=|keys 1 clicks 0', '5:idle=300',
             '6:menu=fullscreen', '7:idle=300',
             *(f'8:{s}' for s in CHORD), '8:wait=|keys 2 clicks 0', '8:idle=300', '9:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_SCREEN'):
        env.pop(key, None)
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree', '--exe', 'C:\\FSBARS.EXE',
               '--quiet', '--drive-c', str(out / 'drive-c'), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=600).returncode
    shutil.rmtree(out / 'drive-c', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    bars = re.findall(r'^NEO98 HOST bars menu=(\w+) dock=(\w+)$', text, re.M)
    hidden, menu, shown = ('hidden', 'hidden'), ('shown', 'hidden'), ('shown', 'shown')
    checks = {
        'full screen hides the menu bar and the Dock, Ctrl+Shift+M shows the menu bar and hides it again, '
        'and out of full screen both are back': bars == [hidden, menu, hidden, shown],
        # Had the chords reached the window, its title would be "keys 3" here.
        'in full screen Ctrl+Shift+M does not reach the window, a plain M does':
            'input wait=|keys 1 clicks 0 reached' in text,
        'out of full screen Ctrl+Shift+M is the window\'s': 'input wait=|keys 2 clicks 0 reached' in text,
        'the host ended': code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'bars': bars, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'FULLSCREEN BARS FAIL: bars {bars}; see {out}')
        return 1
    print('FULLSCREEN BARS PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
