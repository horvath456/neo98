#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explorer/DOS activation after a client click, including the real taskbar.

Runs the original shell and COMMAND.COM on a private first-logon overlay.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import first_logon
import win98_c
from win98_folder import journal_record


# Win98 SE draws captions as a gradient (COLOR_ACTIVECAPTION #000080 to
# COLOR_GRADIENTACTIVECAPTION #1084D0); the inactive one runs grey.
def active_caption(p):
    return p[0] <= 0x10 and p[2] >= 0x80 and p[2] - p[0] >= 0x70


def pixels(path):
    magic, size, maximum, data = path.read_bytes().split(b'\n', 3)
    assert magic == b'P6' and size == b'800 600' and maximum == b'255'
    return lambda x, y: tuple(data[(y * 800 + x) * 3:(y * 800 + x) * 3 + 3])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/dos-taskbar-host')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    drive, registry = first_logon.clone(out / 'session')
    # Mounted host CDs must not launch unrelated autorun windows in this test.
    policy = r'HKEY_USERS\.Default\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer'
    registry.write_bytes(registry.read_bytes() + journal_record(3, policy, 'NoDriveTypeAutoRun')
                         + struct.pack('<III', 4, 4, 255))
    names = ('dos', 'explorer', 'dos-back', 'explorer-again')
    for name in names:
        (out / f'{name}.ppm').unlink(missing_ok=True)
    def shot(t, name):
        return [f'{t}:shot={out}/{name}.ppm', f'{t}:tops=1']
    steps = ['0:wait=Shell_TrayWnd', '1:dblclick=38,22', '2:wait=CabinetWClass|My Computer',
             '3:dblclick=300,66', r'5:guestopen=C:\WINDOWS\COMMAND.COM', '6:wait=tty',
             *shot(8, 'dos'), '9:click=740,500', *shot(11, 'explorer'),
             '12:keydown=12', '12:key=46', '12:keyup=12', '12:wait=#32768', '13:key=1b',
             '14:click=400,585', *shot(16, 'dos-back'),
             '17:click=740,500', *shot(19, 'explorer-again'), '21:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    try:
        with (out / 'host.log').open('w') as log:
            code = subprocess.run([str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                '--system-tree', str(win98_c.use_pristine()), '--drive-c', str(drive),
                '--registry', str(registry), '--desktop', '800x600', '--exe', r'C:\WINDOWS\EXPLORER.EXE',
                '--quiet', '--crash-report-dir', str(out / 'reports')], cwd=ROOT, env=env,
                stdout=log, stderr=subprocess.STDOUT, timeout=90).returncode
        text = (out / 'host.log').read_text(errors='replace')
        checks = {'normal exit': code == 0 and not any(s in text for s in
                  ('CRASHREPORT', 'NEO98 HOST failed', 'timed out', 'deadline reached'))}
        checks['Explorer receives Alt+F after the client click'] = 'input wait=#32768 reached' in text
        for name in names:
            path = out / f'{name}.ppm'
            checks[f'{name}: screenshot exists'] = path.is_file()
            if not path.is_file():
                continue
            pixel = pixels(path)
            dos = name.startswith('dos')
            checks[f'{name}: active title bar'] = active_caption(pixel(350, 62 if dos else 8))
            checks[f'{name}: Explorer task button'] = (pixel(180, 576)[0] < 128) == (not dos)
            checks[f'{name}: DOS task button'] = (pixel(370, 576)[0] < 128) == dos
            section = text.split(f'screenshot {path}', 1)[-1].split('NEO98 HOST input ', 1)[0]
            state = re.search(r'tops foreground=([0-9a-f]+) focus=([0-9a-f]+)', section)
            cls = 'tty' if dos else 'CabinetWClass'
            window = re.search(r'top id=([0-9a-f]+) visible=1 .*class=' + cls + r' title=', section)
            checks[f'{name}: foreground belongs to {cls}'] = bool(state and window and state[1] == window[1])
        report = {'host': str(args.host.resolve()), 'exit': code, 'checks': checks,
                  'status': 'PASS' if all(checks.values()) else 'FAIL'}
        (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        for name, ok in checks.items():
            print(('ok  ' if ok else 'FAIL '), name)
        return 0 if all(checks.values()) else 1
    finally:
        shutil.rmtree(out / 'session', ignore_errors=True)


if __name__ == '__main__':
    raise SystemExit(main())
