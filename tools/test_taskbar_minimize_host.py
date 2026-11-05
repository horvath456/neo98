#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The taskbar button of a minimized window, and opening that folder again.

1. My Computer opened from the desktop: its taskbar button is pressed. The
   Minimize button: the activation goes to Progman, the desktop, a tool
   window; USER then calls the shell hook with
   HSHELL_WINDOWACTIVATED and no window, and the taskbar lifts the button
   (src/user.cpp NotifyActivation; the queue switch of
   src/x86_activation.cpp SwitchForeground reports a window that was the
   active one of its queue already). A click on the button restores the
   window and presses it again.
2. A double click on My Computer while its window is minimized: Explorer
   ended with a fetch from the exception guard token 71000E00 (
   tools/test_exception_guard_guest.py).
On the desktop of the first logon, without a window.

    build/analysis-venv/bin/python tools/test_taskbar_minimize_host.py [--no-build]
"""
from __future__ import annotations

import os
import subprocess
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import first_logon  # noqa: E402
from win98_folder import journal_record  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402

# The first taskbar button, a row of its top edge: dark when pressed, white
# when raised, on the 800 by 600 canvas.
BUTTON = (180, 600 - 24)


def pressed(path):
    data = path.read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    x, y = BUTTON
    return parts[3][(y * width + x) * 3] < 128


def main():
    out = neo98.output_directory('taskbar-minimize-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive, registry = first_logon.clone(out / 'session')
    policy = r'HKEY_USERS\.Default\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer'
    registry.write_bytes(registry.read_bytes() + journal_record(3, policy, 'NoDriveTypeAutoRun')
                         + struct.pack('<III', 4, 4, 255))
    # My Computer at 38,22 on the desktop; its window at 64,55..576,512 with
    # the Minimize button at 530,66; the taskbar button at 220,585.
    steps = ['0:wait=Shell_TrayWnd', '1:dblclick=38,22', '4:wait=CabinetWClass|My Computer',
             f'6:shot={out}/open.ppm', '7:click=530,66', f'9:shot={out}/min.ppm', '10:click=220,585',
             f'13:shot={out}/back.ppm', '14:click=530,66', '16:dblclick=38,22', '21:tops=1']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='50', NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_TRACE_CALLS_FROM'):
        env.pop(key, None)
    with (out / 'host.log').open('w') as log:
        code = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--registry', str(registry),
                               '--crash-report-dir', str(out / 'reports')], cwd=ROOT, env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=240).returncode
    text = (out / 'host.log').read_text(errors='replace')
    shots = {n: out / f'{n}.ppm' for n in ('open', 'min', 'back')}
    have = all(p.is_file() for p in shots.values())
    checks = {
        'the open window has its button pressed': have and pressed(shots['open']),
        'minimized, the button is raised': have and not pressed(shots['min']),
        'restored from the taskbar, pressed again': have and pressed(shots['back']),
        'a double click on My Computer while minimized brings the window back':
            'NEO98 HOST failed' not in text and 'CRASHREPORT' not in text
            and 'class=CabinetWClass title=My Computer' in text.split('NEO98 HOST tops')[-1],
        'the host ends normally': code == 0,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass, log {out / "host.log"}')
    subprocess.run(['rm', '-rf', str(out / 'session')])
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
