#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explorer with the taskbar at the top: a maximized window lies in the work area.

The real case behind the work area does not begin at the upper edge
of the screen. Explorer runs as the shell without a screen; the script drags
the taskbar from the lower edge to the upper one, as a hand does, opens the
original Notepad and maximizes it with ALT+SPACE, X. USER places the
maximized window at the work area less the sizing frame :
with the taskbar above, its upper edge is the lower edge of the taskbar less
four points and not minus four.

    build/analysis-venv/bin/python tools/test_taskbar_top_host.py [--no-build]

Pictures and logs in build/taskbar-top-host/.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
import host_window_lists as lists  # noqa: E402
win98_c.use_pristine()

OUT = ROOT / 'build/taskbar-top-host'


def main():
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    drive_c = OUT / 'drive-c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    shots = OUT / 'shots'
    shots.mkdir()
    steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', f'2:shot={shots}/00-taskbar-below.ppm', '2.2:tops=1', '2.3:windows=1',
             # An empty place of the taskbar, between the buttons and the clock.
             '3:down=500,590', '3.3:move=500,560', '3.6:move=500,400', '3.9:move=500,150', '4.2:move=500,20',
             '4.5:move=500,6', '4.8:up=500,6', '5.5:idle=1000', f'7:shot={shots}/01-taskbar-above.ppm', '7.2:tops=1',
             '7.3:windows=1',
             '8:guestopen=C:\\WINDOWS\\NOTEPAD.EXE', '8.1:wait=Notepad', '9:idle=1000', f'10:shot={shots}/02-notepad.ppm',
             '10.2:tops=1',
             '11:keydown=12', '11.2:key=20', '11.4:keyup=12', '12:key=58', '13:idle=1000',
             f'14:shot={shots}/03-notepad-maximized.ppm', '14.2:tops=1', '14.3:windows=1', '15:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='240', NEO98_SETTINGS_DIR=str(OUT / 'settings'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_HOST_SCREEN'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', '--exe',
               'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(OUT / 'reports')]
    log = OUT / 'host.log'
    with log.open('w') as stream:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log.read_text(errors='replace')
    listed = lists.tops(text)
    work = [[int(n) for n in match] for match in re.findall(r'NEO98 HOST seamless work=(-?\d+),(-?\d+),(-?\d+),(-?\d+)', text)]
    results = {'host': result.returncode, 'work areas': work, 'lists': len(listed)}
    checks = {'the host ended in order': result.returncode == 0,
              'the run listed its windows four times and named the work area three times': len(listed) == 4 and len(work) == 3}
    if len(listed) == 4 and len(work) == 3:
        below, above = lists.window(listed[0], 'Shell_TrayWnd'), lists.window(listed[1], 'Shell_TrayWnd')
        notepad = lists.window(listed[3], 'Notepad')
        results.update({'taskbar before': below, 'taskbar after': above, 'notepad maximized': notepad})
        checks['at the start the taskbar lies at the lower edge and the work area begins at 0'] = \
            bool(below) and below['frame'][3] >= 600 and below['frame'][1] > 500 and work[0][1] == 0 and work[0][3] < 600
        checks['after the drag it lies at the upper edge'] = bool(above) and above['frame'][1] <= 0 and above['frame'][3] < 100
        checks['and the work area begins below it and ends at the lower edge of the screen'] = \
            bool(above) and work[1][1] > 0 and work[1][1] == above['frame'][3] and work[1][3] == 600
        want = [work[2][0] - 4, work[2][1] - 4, work[2][2] + 4, work[2][3] + 4]
        results['notepad wanted'] = want
        checks[f'the maximized Notepad is the work area less the sizing frame: {want}'] = \
            bool(notepad) and bool(notepad['style'] & 0x01000000) and notepad['frame'] == want
    results['checks'] = checks
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(f'pictures in {shots}, log {log}')
    if not all(checks.values()):
        print(f'TASKBAR TOP FAIL {json.dumps({k: v for k, v in results.items() if k != "checks"})}')
        return 1
    print('TASKBAR TOP PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
