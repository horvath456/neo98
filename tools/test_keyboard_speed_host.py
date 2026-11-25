#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Keyboard control panel of Windows 98 can change the speed.

The page Speed of MAIN.CPL loads the 16-bit KEYBOARD with LoadLibrary16 and
looks for its SetSpeed with GetProcAddress16; SetSpeed(-1) asks whether the
driver can set the typematic rate. KERNEL32's 16-bit module API handed out
no KEYBOARD, and the applet said "You cannot change the speed for your
keyboard because your keyboard driver does not support this feature."

On neo98-kms over a private copy of drive C:, with the picture of a 1024x768
desktop in memory: Keyboard Properties opens without that message, Apply
takes the speed, and the sheet closes with OK.

    build/analysis-venv/bin/python tools/test_keyboard_speed_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/keyboard-speed'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
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
    # Repeat rate is the second slider of the page; End moves it to Fast.
    # Alt+A is Apply, Enter is OK.
    steps = ['0:wait=#32770', '4:shot={out}/speed.png', '5:key=09', '6:key=09', '7:key=23', '8:alt=41',
             '10:shot={out}/applied.png', '11:key=0d', '14:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
                                 '--command', 'rundll32.exe shell32.dll,Control_RunDLL main.cpl,@1',
                                 '--time-limit', '30'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=120)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    text = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(text)
    stops = [line for line in text.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    boxes = [line for line in text.splitlines() if line.startswith('MessageBox caption=')]
    checks = {
        'Keyboard Properties opens without a stop': not stops and (WORK / 'speed.png').is_file(),
        f'no message that the driver cannot change the speed: {boxes}':
            not any('cannot change the speed' in line for line in boxes),
        'Apply and OK take the speed without a message': not boxes and (WORK / 'applied.png').is_file(),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'keyboard speed host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
