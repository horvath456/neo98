#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The pointer shows the cursor of the queue under it.

Win98 keeps the cursor in the message queue, and the pointer shows the one of
the queue whose window it is over. A program that sets its cursor only when
GetCursor answers another one, as Internet Explorer does over a link, relies
on that: GetCursor answers the cursor of the caller's own queue. The port
showed the cursor the last thread had set, whichever it was, and in a
folder's web view the hand over a link came only the first time; after the
pointer had been over a window of another thread, the link kept the arrow.

tools/queue_cursor_fixture.c: window A of the main thread sets the cross on
WM_SETCURSOR only when GetCursor answers another cursor; window B of a
second thread has the arrow. The pointer goes into A, over to B and back
into A, and A asks GetCursorInfo at each move which cursor shows: the cross
every time.

    build/analysis-venv/bin/python tools/test_queue_cursor_host.py [--no-build]

Results in build/queue-cursor-host/results.json.
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


def main():
    out = neo98.output_directory('queue-cursor-host')
    for name in ('drive-c', 'settings', 'reports'):
        shutil.rmtree(out / name, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if sys.platform == 'darwin' else ['--reflink=auto']), '-R',
                    str(win98_c.FOLDER.resolve()), str(out / 'drive-c')], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/queue_cursor_fixture.c'), '-O1', '-ffreestanding',
                    '-fno-builtin', '-nostdlib', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(out / 'drive-c/QCURSOR.EXE')],
                   check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # Into A, over to B, back into A: two moves in A each time.
    # Into A, over to B, back into A. windows=1 prints the titles.
    steps = ['0:wait=|moves 0 wrong 0', '0:wait=QueueCursorB', '0:idle=300',
             '1:move=100,100', '1:move=110,100', '1:idle=300', '1:windows=1',
             '2:move=400,100', '2:move=410,100', '2:idle=300',
             '3:move=150,100', '3:move=160,100', '3:idle=300', '3:windows=1', '4:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'), NEO98_HOST_DEADLINE='120')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_SCREEN'):
        env.pop(key, None)
    command = [str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--system-tree', '--exe', 'C:\\QCURSOR.EXE',
               '--quiet', '--drive-c', str(out / 'drive-c'), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        code = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                              stdin=subprocess.DEVNULL, timeout=300).returncode
    shutil.rmtree(out / 'drive-c', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    titles = re.findall(r'^NEO98 HOST seamless item .* title=(moves \d+ wrong \d+)$', text, re.M)
    counts = [tuple(int(v) for v in re.findall(r'\d+', t)) for t in titles]
    # Moves that follow each other closely reach A as one (USER keeps only
    # the last WM_MOUSEMOVE in the queue).
    checks = {
        'the first time in A the pointer shows the cross': len(counts) >= 1 and counts[0][0] >= 1 and counts[0][1] == 0,
        'back in A after B the pointer shows the cross again':
            len(counts) >= 2 and counts[1][0] > counts[0][0] and counts[1][1] == 0,
        'the host ended': code == 0,
    }
    (out / 'results.json').write_text(json.dumps({'titles': titles, 'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'QUEUE CURSOR FAIL: titles {titles}; see {out}')
        return 1
    print('QUEUE CURSOR PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
