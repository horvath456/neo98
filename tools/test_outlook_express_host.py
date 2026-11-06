#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Outlook Express opens from the Win98 desktop.

Explorer is the shell, the view is the seamless one, no screen. The script
opens C:\\Program Files\\Outlook Express\\MSIMN.EXE the way a double click
does and waits for the Internet Connection Wizard, which the first start of
Outlook Express shows over its main window. Escape and "Yes" end the wizard;
a click on "Local Folders" in the folder list changes the title of the main
window to "Local Folders - Outlook Express".

Before, the first start from setup's C: ended with "The Address Book failed
to load" (the registry of the machine lacked HKLM\\Software\\Microsoft\\WAB,
). The second start, with the stores of the first one on the drive,
can stand with unpainted windows (tools/test_foreground_sleeping_thread_
host.py, has the program for that); the script closes Outlook Express and
starts it again. A store whose view read zeros behind its header made
DIRECTDB repair it, with its progress dialog, and the folder list then chose
"Local Folders" without opening it.

    build/analysis-venv/bin/python tools/test_outlook_express_host.py [--no-build]
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

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

SCREEN = (1024, 768)


def main():
    out = neo98.output_directory('outlook-express-host')
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    program = drive / 'Program Files/Outlook Express/MSIMN.EXE'
    wizard = ['0:wait=|Internet Connection Wizard', '0:idle=1000', '1:key=1b', '2:idle=500', '3:key=59',
              '4:idle=1000', '5:windows=1']
    steps = ['0:wait=Shell_TrayWnd', '0:idle=1500', f'0:open={program}', *wizard,
             '7:idle=500', '8:folder=', '9:wait=|Local Folders - Outlook Express',
             '9:windows=1',
             # Close it (Alt+F4) and start it again: the stores exist now.
             '10:keydown=12', '10:key=73', '10:keyup=12', '11:idle=1500', '12:windows=1',
             # The wizard comes with the first start only.
             f'13:open={program}', '13:wait=Outlook Express Browser Class', '14:idle=1000', '15:windows=1', '16:menu=quit']
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports'), '--seamless']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_SCREEN='%dx%d' % SCREEN,
               NEO98_HOST_DEADLINE='420', NEO98_DOCK_HELPERS='off', NEO98_SETTINGS_DIR=str(out / 'settings'),
               NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / 'host.log'

    def run(script):
        with log_path.open('w') as log:
            try:
                return subprocess.run(command, cwd=ROOT, env=dict(env, NEO98_HOST_INPUT=';'.join(script)), stdout=log,
                                      stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=480).returncode
            except subprocess.TimeoutExpired:
                return 'timeout'

    # The main window is 672x480 in the middle of the screen; "Local Folders"
    # is the second line of its folder list.
    left, top = (SCREEN[0] - 672) // 2, (SCREEN[1] - 480) // 2
    steps = [s if s != '8:folder=' else f'8:click={left + 71},{top + 166}' for s in steps]
    code = run(steps)
    shutil.rmtree(drive, ignore_errors=True)
    shutil.rmtree(out / 'settings', ignore_errors=True)
    text = log_path.read_text(errors='replace')
    waits = re.findall(r'^NEO98 HOST input wait=\|?(\S.*) (reached|timed out) after', text, re.M)
    lists, current = [], None
    for line in text.splitlines():
        if line.startswith('NEO98 HOST seamless items '):
            current = []
            lists.append(current)
        elif line.startswith('NEO98 HOST seamless item ') and current is not None:
            current.append(line.rsplit('title=', 1)[1])
    frame = re.search(r'^NEO98 HOST seamless item id=\w+ frame=(\d+),(\d+),(\d+),(\d+) .*title=Outlook Express$', text, re.M)
    checks = {
        'host ended': code == 0,
        'every wait reached': len(waits) == 4 and all(w[1] == 'reached' for w in waits),
        'the address book loads': 'The Address Book failed to load' not in text,
        # DIRECTDB shows its dialog with the progress bar (362x152) when it has
        # to repair a store. The stores of this run are new and intact; the
        # dialog came when a view of a store read zeros behind its header
        # at the click on "Local Folders" or at the second start.
        'no store needs a repair': not re.search(r'^CreateWindow class=#32770 .* w=362 h=152$', text, re.M),
        # The first start of the shell from setup's C: reports short hangs now
        # and then, before Outlook Express is opened; none may stay.
        'no hang stays': text.rfind('NEO98 HOST hang begin') <= text.rfind('NEO98 HOST hang end'),
        'four window lists': len(lists) == 4,
        'the main window is where the click expects it': bool(frame) and (int(frame.group(1)), int(frame.group(2))) == (left, top),
    }
    if len(lists) == 4:
        checks['after the wizard the main window stands alone'] = 'Outlook Express' in lists[0] and \
            'Internet Connection Wizard' not in lists[0]
        checks['a click in the folder list opens Local Folders'] = 'Local Folders - Outlook Express' in lists[1]
        checks['Alt+F4 closes Outlook Express'] = not any('Outlook Express' in title for title in lists[2])
        checks['the second start opens the main window again'] = 'Outlook Express' in lists[3]
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': code, 'checks': checks},
                                                 indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'OUTLOOK EXPRESS FAIL; see {log_path}')
        return 1
    print('OUTLOOK EXPRESS PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
