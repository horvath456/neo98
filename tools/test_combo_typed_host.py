#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Text typed into a combo box is no item of its list.

When the user changes the text of a drop-down combo box, USER sends
CBN_EDITCHANGE and then lets the list follow the text: it selects no item
(LB_SETCURSEL with -1) and moves the caret and the top of the list to the
first item that begins with the text. CB_GETCURSEL answers CB_ERR from then
on, until an item is chosen again. The port left the old item selected.

Regional Settings reads each separator box as "the selected item, or else
the text": with ":" selected and "." typed it stored ":" again, so a typed
separator was never taken.

tools/combo_typed_fixture.c in neo98-kms over a private copy of drive C:
types into the edit field of a combo box with the items ":", "." and "-":
the first item is selected at first; after the text of the second item was
typed no item is selected, the text is the typed one and the parent got
CBN_EDITCHANGE once; other text selects nothing either; CB_SETCURSEL
selects an item again and puts its text into the field.

    build/analysis-venv/bin/python tools/test_combo_typed_host.py [--no-build] [--keep] [--kms KMS]
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
WORK = ROOT / 'build/combo-typed'
EXPECTED = {
    'selected at first': '0',
    'edit field found': '1',
    "selected after typing an item's text": '-1',
    'text is what was typed': '1',
    'CBN_EDITCHANGE': '1',
    'selected after typing other text': '-1',
    'selected after choosing again': '2',
    'text is the chosen item': '1',
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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/combo_typed_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'COMBOTYP.EXE')], check=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:800x600')
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD', 'NEO98_HOST_INPUT'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '800x600',
                                 '--exe', 'C:\\COMBOTYP.EXE', '--time-limit', '60'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=120)
        (WORK / 'log.txt').write_text(f'{result.returncode}\n{result.stdout}{result.stderr}')
        written = win98_c.path('C:\\COMBOTYP.TXT', drive)
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
    print(f'combo typed host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
