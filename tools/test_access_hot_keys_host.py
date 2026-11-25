#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The hot keys of MouseKeys and FilterKeys, on neo98-kms.

The shortcuts of ACCESS.CPL, which the confirmation of ENABLE3.DLL names:
Left Alt+Left Shift+Num Lock turns MouseKeys on or off, and Right Shift held
for eight seconds turns FilterKeys on. Neither did anything here.
tools/access_hot_keys_fixture.c turns both features off with their hot keys
active and notes each change of them:

  mouse    Left Alt+Left Shift+Num Lock turns MouseKeys on ("M1")
  filter   Right Shift held for three seconds and repeated once does nothing;
           held for nine seconds, the keyboard repeating it every three, it
           turns FilterKeys on ("F1")
  again    Left Alt+Left Shift+Num Lock turns MouseKeys off again ("M0")

Esc writes the notes and ends the fixture, and with it the host.

    build/analysis-venv/bin/python tools/test_access_hot_keys_host.py [--no-build] [--kms KMS]
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
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/access-hot-keys'


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/access_hot_keys_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'HOTKEYS.EXE')], check=True)
    # The keys follow each other 0.3 s apart and each chord waits for a quiet
    # machine. Right Shift going down again while it is held is a repeat of
    # it, here every three seconds as a slow keyboard repeats it. Esc writes
    # the notes and ends the fixture, and with it the host.
    mouse = [(0.3, 'keydown=a4'), (0.3, 'keydown=a0'), (0.3, 'keydown=90'), (0.3, 'keyup=90'), (0.3, 'keyup=a0'),
             (0.3, 'keyup=a4'), (0.3, 'idle=500')]
    short = [(0.3, 'keydown=a1'), (3, 'keydown=a1'), (0.3, 'keyup=a1'), (0.3, 'idle=500')]
    held = [(0.3, 'keydown=a1'), (3, 'keydown=a1'), (3, 'keydown=a1'), (3, 'keydown=a1'), (0.3, 'keyup=a1'),
            (0.3, 'idle=500')]
    steps = ['0:wait=AccessHotKeys', '0:idle=1000']
    at = 0.0
    for delta, step in mouse + short + held + mouse + [(0.3, 'key=1b')]:
        at += delta
        steps.append(f'{at:.1f}:{step}')
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\HOTKEYS.EXE', '--time-limit', '120'], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=150)
        answer = win98_c.path('C:\\HOTKEYS.TXT', drive)
        notes = answer.read_text().split() if answer else None
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(result.stdout + result.stderr)
    timed_out = [line for line in (result.stdout + result.stderr).splitlines() if 'timed out after' in line]
    checks = {
        f'the fixture ended the run itself, before the time limit: {result.returncode}': result.returncode == 0,
        f'every quiet moment came: {timed_out}': not timed_out,
        f'the hot keys turned MouseKeys on, FilterKeys on after eight seconds only, MouseKeys off: {notes}':
            notes == ['M1', 'F1', 'M0'],
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'access hot keys: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
