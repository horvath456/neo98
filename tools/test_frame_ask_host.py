#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A window is asked before a press on its frame and when the work area changes.

Windows asks a window with WM_NCHITTEST before every press on its frame, so
a procedure that answers HTCAPTION over the minimize box gets a press on the
caption there. The port pressed a caption button wherever its rectangle was,
whatever the window had answered, and the window was minimized all the same.

When the work area changes, USER places the windows of the top level anew
with DeferWindowPos: a maximized window is asked with WM_WINDOWPOSCHANGING
and told with WM_WINDOWPOSCHANGED and WM_SIZE. The port moved it without a
message, so a program that keeps its own place in WM_WINDOWPOSCHANGING, or
lays its client area out in WM_SIZE, never heard of it.

tools/frame_ask_fixture.c in neo98-kms over a private copy of drive C::

  a press and a release over the minimize box of a window whose procedure
  answers HTCAPTION there: the window is not minimized, gets
  WM_NCLBUTTONDOWN with HTCAPTION once, no press on a box and no SC_MINIMIZE
  the same over the box of a plain window minimizes it
  the plain window maximized, the work area 40 pixels lower at the bottom:
  WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED and WM_SIZE once each, the bottom
  of the window 40 pixels higher, the other sides as they were, and all of
  it back with the old work area

    build/analysis-venv/bin/python tools/test_frame_ask_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
if __name__ == '__main__':
    import shutil as _shutil
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
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
WORK = ROOT / 'build/frame-ask'
EXPECTED = {
    'minimize box found': '1',
    'asked window minimized': '0',
    'asked window got the press on its caption': '1',
    'asked window got a press on a box': '0',
    'asked window got SC_MINIMIZE': '0',
    'plain window minimized': '1',
    'work area set': '1',
    'WM_WINDOWPOSCHANGING': '1',
    'WM_WINDOWPOSCHANGED': '1',
    'WM_SIZE': '1',
    'bottom': '-40',
    'other sides': '0',
    'back': '1',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the copy of the drive')
    parser.add_argument('--kms', type=Path, default=BINARY)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(vulkan=False)
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    drive = WORK / 'c'
    pristine = win98_c.use_pristine()
    subprocess.run(['cp', '-a', '--reflink=auto', str(pristine), str(drive)], check=True)
    if win98_c.journal(pristine).is_file():
        shutil.copy2(win98_c.journal(pristine), win98_c.journal(drive))
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/frame_ask_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'FRAMEASK.EXE')], check=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\FRAMEASK.EXE', '--time-limit', '60'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
        (WORK / 'log.txt').write_text(f'{result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\FRAMEASK.TXT', drive)
        found = dict(line.split('=', 1) for line in (written.read_text().splitlines() if written else ())
                     if '=' in line)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
            Path(str(drive) + '.clock').unlink(missing_ok=True)
    checks = {f'{name}: {found.get(name)} (expected {wanted})': found.get(name) == wanted
              for name, wanted in EXPECTED.items()}
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'frame ask host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
