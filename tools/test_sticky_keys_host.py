#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""StickyKeys acts on the keys, on neo98-kms.

SPI_SETSTICKYKEYS kept the settings of StickyKeys without acting on input.
tools/sticky_keys_fixture.c starts with StickyKeys off, its hot key active
and three states (SKF_TRISTATE), and keeps the characters it gets:

  hot key   Shift pressed five times turns StickyKeys on; "a" stays small
  latch     Shift pressed alone holds for the next key only: "Aa"
  lock      Shift pressed twice holds until a third press: "AA", then "a"
  control   Ctrl pressed alone makes the next C a Ctrl+C: "^C", then "c"
  off       Shift pressed five times turns it off again: Shift alone does
            nothing to "a", and SPI_GETSTICKYKEYS says it is off

Before, every letter came small and the hot key did nothing.

    build/analysis-venv/bin/python tools/test_sticky_keys_host.py [--no-build] [--kms KMS]
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
WORK = ROOT / 'build/sticky-keys'
SHIFT, CONTROL, A, C, ESCAPE = '10', '11', '41', '43', '1b'


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/sticky_keys_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'STICKY.EXE')], check=True)
    keys = [SHIFT] * 5 + [A, SHIFT, A, A, SHIFT, SHIFT, A, A, SHIFT, A, CONTROL, C, C] + [SHIFT] * 6 + [A, ESCAPE]
    # Esc writes the answer and ends the fixture, and with it the host: no
    # step quits at a time the fixture may not have reached on a busy machine.
    steps = ['0:wait=StickyTest'] + [f'{1 + i * 0.2:.1f}:key={key}' for i, key in enumerate(keys)]
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\STICKY.EXE', '--time-limit', '90'], cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=150)
        answer = win98_c.path('C:\\STICKY.TXT', drive)
        lines = answer.read_text().split() if answer else []
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(result.stdout + result.stderr)
    typed = lines[0] if lines else ''
    flags = int(lines[1], 16) if len(lines) > 1 else None
    checks = {
        f'the fixture ended the run itself, before the time limit: {result.returncode}': result.returncode == 0,
        f'the fixture got the keys: "{typed}"': len(lines) == 2,
        'hot key: five presses of Shift turn StickyKeys on, "a" stays small': typed[:1] == 'a',
        'latch: Shift alone holds for the next key only, "Aa"': typed[1:3] == 'Aa',
        'lock: Shift twice holds until a third press, "AA" then "a"': typed[3:6] == 'AAa',
        'control: Ctrl alone makes the next C a Ctrl+C, "^C" then "c"': typed[6:9] == '^Cc',
        f'off: five presses of Shift turn it off: "a", flags {flags if flags is None else hex(flags)}':
            typed[9:] == 'a' and flags is not None and not flags & 1,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'sticky keys: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
