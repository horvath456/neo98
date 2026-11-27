#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The system menu of a window shows the states of the window.

USER sets the items of a system menu from the window each time the menu is
about to be shown: Restore for a minimized or maximized window, no Move, Size
and Maximize for a maximized one, no Size and Minimize for a minimized one,
Size, Minimize and Maximize only with their styles, and Close as the default
item, Restore for a minimized window. The port set them once, when the menu
was made, so a maximized window offered Maximize and grayed Restore.

tools/system_menu_fixture.c in neo98-kms over a private copy of drive C:
opens the system menu of its window with SC_KEYMENU in five states and
writes down what WM_INITMENUPOPUP finds:

  normal      Restore grayed, the rest enabled, Close the default item
  maximized   Restore enabled; Move, Size and Maximize grayed. Before the
              menu is opened its items are still those of the last opening:
              GetSystemMenu alone sets nothing
  minimized   Restore enabled and the default item; Move (the window is the
              taskbar's), Size and Minimize grayed
  restored    as the normal window again
  no boxes    a caption with a system menu only: Size, Minimize and Maximize
              grayed

    build/analysis-venv/bin/python tools/test_system_menu_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/system-menu'
# Restore, Move, Size, Minimize, Maximize: 1 is grayed; then the default item.
EXPECTED = {
    'normal': '10000 f060',
    'maximized, not opened': '10000 f060',
    'maximized': '01101 f060',
    'minimized': '01110 f120',
    'restored': '10000 f060',
    'no boxes': '10111 f060',
    'opened': '5',
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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/system_menu_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'SYSMENU.EXE')], check=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\SYSMENU.EXE', '--time-limit', '60'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
        (WORK / 'log.txt').write_text(f'{result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\SYSMENU.TXT', drive)
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
    print(f'system menu host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
