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
  passwords  Passwords in the Control Panel, which the fixture opens, has the
             page Change Passwords beside User Profiles (User Profiles alone
             before)

The steps wait for the Start menu, the Run box, Passwords Properties and a
quiet machine, not for a time.

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
    # Start > Run of the fixture, which opens Passwords: each step waits for
    # the Start menu and the Run box, the picture for Passwords Properties
    # and a quiet machine.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=500', f'1:click={START[0]},{START[1]}', '1:wait=BaseBar', '1.5:type=r',
             '1.5:wait=#32770|Run', '1.5:idle=300', '2:type=C:\\WNETUSER.EXE', '2.5:key=0d',
             '2.5:wait=#32770|Passwords Properties', '2.5:idle=500', f'2.5:shot={shot}', '3:quit=1']
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
    timed_out = [line for line in text.splitlines() if 'timed out after' in line]
    checks = {
        'start: the shell starts without asking for a password': 'NEO98 KMS input wait=Shell_TrayWnd\n' in text,
        f'every menu, dialog and quiet moment came: {timed_out}': not timed_out,
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
