#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Display Properties retains group-box backgrounds across live depth changes."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import win98_c
from test_combo_savebits_host import pixels, differing


def main():
    out = ROOT / 'build/display-switch-host'
    out.mkdir(parents=True, exist_ok=True)
    drive = out / 'c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.use_pristine()), str(drive)], check=True)
    shots = {name: out / (name + '.ppm') for name in ('before', 'indexed', 'restored')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    steps = ['0:wait=|Display Properties', '1:idle=500', '2:move=700,540', f'2.5:shot={shots["before"]}']
    for name, key, start in (('indexed', '24', 3), ('restored', '23', 13)):
        steps += [f'{start}:click=240,382', f'{start+1}:key={key}', f'{start+1.2}:key=0d',
                  f'{start+2}:button=Apply', f'{start+3}:wait=|Compatibility Warning',
                  f'{start+4}:button=Apply the new color settings without restarting?',
                  f'{start+5}:button=OK', f'{start+6}:idle=500',
                  f'{start+7}:move=700,540', f'{start+8}:shot={shots[name]}']
    steps += ['22:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='90',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    env.pop('NEO98_HOST_ATTACHED', None)
    env.pop('NEO98_HOST_MENU', None)
    command = [str(ROOT / 'build/native-app/neo98-host'), '-',
               str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', str(drive),
               '--drive-c', str(drive), '--exe', r'C:\WINDOWS\RUNDLL32.EXE',
               '--command', 'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,3',
               '--no-registry-file', '--quiet', '--crash-report-dir', str(out / 'reports')]
    with (out / 'host.log').open('w') as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=110)
    log = (out / 'host.log').read_text(errors='replace')
    assert result.returncode == 0 and 'timed out' not in log, log[-4000:]
    before = pixels(shots['before'])
    checks = {}
    for name in ('indexed', 'restored'):
        after = pixels(shots[name])
        expected = before
        if name == 'indexed':
            # Win98 snaps the two gray edge colors in the indexed mode.
            # The display-bitmap fixture also checks COLOR_3DLIGHT and its
            # actual device-pixel conversion against the original system.
            body, width = before
            converted = b''.join(b'\0\0\0' if body[i:i+3] == b'@@@' else
                                 b'\xc0\xc0\xc0' if body[i:i+3] == b'\xdf\xdf\xdf' else body[i:i+3]
                                 for i in range(0, len(body), 3))
            expected = converted, width
        # The screen-area group and the transparent interior below the colour
        # selector must survive the warning dialog and the mode switch.
        for area, rect in [('screen area', (271, 356, 441, 421)),
                           ('colour group interior', (100, 400, 245, 410))]:
            checks[f'{name}: {area}'] = differing(expected, after, rect) == 0
    checks['both changes reached the display driver'] = log.count('DDRAWDRIVER display mode') == 2
    (out / 'results.json').write_text(json.dumps(checks, indent=2) + '\n')
    for name, okay in checks.items():
        print(('PASS ' if okay else 'FAIL ') + name)
    assert all(checks.values()), checks
    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
