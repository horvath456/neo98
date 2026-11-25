#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Windows 98 logs the user on before the shell starts, on neo98-kms.

MSGSRV32.EXE calls USER's WNetLogon before it starts the shell of
SYSTEM.INI, and MPR logs on through MPREXE: MPRSERV takes the primary
network logon (Windows Logon) and the password list of the user of
Network\\Logon. Nothing here did, so MPR answered every request that needs a
logged on user with an error. Setup leaves the user no password list; the
first start of a real Windows 98 under Windows Logon asks for a password,
and an empty one makes the list. Over a copy of setup's drive C: Explorer
starts as the shell:

  start      the shell starts without asking for a password, and the user
             NEO98 has a password list (C:\\WINDOWS\\NEO98.PWL, named under
             [Password Lists] of SYSTEM.INI)
  user       tools/wnet_user_fixture.c, run from Start > Run, gets the user
             from WNetGetUserA (error 1245, not logged on, before)
  passwords  Passwords in the Control Panel has the page Change Passwords
             beside User Profiles (User Profiles alone before)

    build/analysis-venv/bin/python tools/test_logon_start_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/logon-start'
# The Start button on 1024x768, and in Passwords Properties at its first
# place the label of the second tab (User Profiles beside Change Passwords)
# and of the button Change Windows Password.
START = (25, 755)
SECOND_TAB, CHANGE = (182, 88, 246, 102), (210, 187, 366, 202)


def dark(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return sum(data[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\0\0\0'
               for y in range(top, bottom) for x in range(left, right))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/wnet_user_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lmpr', '-luser32', '-lkernel32',
                    '-o', str(drive / 'WNETUSER.EXE')], check=True)
    shot = WORK / 'passwords.png'
    run = [f'{{t}}:click={START[0]},{START[1]}', '{t1}:type=r', '{t2}:wait=#32770', '{t3}:type={command}',
           '{t4}:key=0d']
    steps = ['0:wait=Shell_TrayWnd',
             *[step.format(t=5, t1=6, t2=7, t3=8, t4=9, command='C:\\WNETUSER.EXE') for step in run],
             *[step.format(t=12, t1=13, t2=14, t3=15, t4=16, command='control password.cpl') for step in run],
             f'22:shot={shot}', '23:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\WINDOWS\\EXPLORER.EXE', '--time-limit', '240'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=300)
        text = result.stdout + result.stderr
        answer = win98_c.path('C:\\WNETUSER.TXT', drive)
        user = answer.read_text(errors='replace').strip() if answer else ''
        password_list = win98_c.path('C:\\WINDOWS\\NEO98.PWL', drive)
        system_ini = win98_c.path('C:\\WINDOWS\\SYSTEM.INI', drive).read_text(errors='replace')
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(text)
    listed = re.search(r'^\[Password Lists\]\s*\nNEO98=C:\\WINDOWS\\NEO98\.PWL', system_ini, re.M | re.I)
    picture = read_png(shot) if shot.is_file() else None
    checks = {
        'start: the shell starts without asking for a password': 'NEO98 KMS input wait=Shell_TrayWnd' in text,
        'start: the user has a password list, named in SYSTEM.INI': password_list is not None and listed,
        f'user: WNetGetUserA answers the user: "{user}"': user == '0 NEO98',
        'passwords: Passwords has the page Change Passwords beside User Profiles':
            picture and dark(picture, SECOND_TAB) > 40 and dark(picture, CHANGE) > 100,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Picture and log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'logon at start: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
