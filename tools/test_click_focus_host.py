#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A click inside the active window leaves the focus where it is.

Windows 98 moves the focus on a click only through the activation of the
window: USER asks the window with WM_MOUSEACTIVATE, activates its top-level
window when that is not the active one, and DefWindowProc gives the focus to
the window on WM_ACTIVATE, or DefDlgProc back to the control that had it.
A click on the empty client area, on a static (HTTRANSPARENT) or on the
caption of the window that is active already changes no focus; a control
such as EDIT takes the focus in its own WM_LBUTTONDOWN. The port gave the
clicked window the focus itself: the window got WM_SETFOCUS without any
activation, its edit lost the focus and the caret, and in a dialog the focus
came back through the dialog manager with all of the edit's text selected.

tools/click_focus_fixture.c makes a window with a focused EDIT and a STATIC,
a plain window in one run and a dialog in the other, and a second window,
Other. On neo98-kms with the picture of a 1024x768 desktop in memory, each
run clicks the empty client area, the static and the caption, then Other,
then the empty client area again.

  inside      the three clicks arrive (two in the client area, one on the
              frame); the window gets neither WM_SETFOCUS nor WM_ACTIVATE,
              the edit keeps the focus and its caret after "abc"
  activation  the last click activates the window again: the plain window
              takes the focus (DefWindowProc), the dialog gives it back to
              its edit (DefDlgProc)

    build/analysis-venv/bin/python tools/test_click_focus_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture; docs/BUILDING.md
    print('SKIP: needs i686-w64-mingw32-gcc')
    _sys.exit(77)
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/click-focus'
# On the screen: the empty client area, the static and the caption of the
# window at 100,100 of 400x300, and the client area of Other.
EMPTY, LABEL, CAPTION, OTHER = (403, 322), (130, 192), (250, 111), (700, 170)


def session(kms, drive, dialog):
    steps = ['0:wait=' + ('#32770' if dialog else 'ClickFocus'), '2:shot={out}/start.png']
    for at, (x, y) in zip((3, 4, 5, 7, 9), (EMPTY, LABEL, CAPTION, OTHER, EMPTY)):
        steps.append(f'{at}:click={x},{y}')
    steps += ['11:shot={out}/end.png', '12:quit=1']
    name = 'dialog' if dialog else 'plain'
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)).replace('.png', f'-{name}.png'))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    result = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', 'C:\\FOCUS.EXE',
                             '--command', 'FOCUS.EXE' + (' dialog' if dialog else ''), '--time-limit', '25'],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
    written = win98_c.path('C:\\FOCUS.TXT', drive)
    data = written.read_bytes() if written else b''
    if written:
        written.unlink()
    return result.stdout + result.stderr, struct.unpack('<20i', data) if len(data) >= 80 else None


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/click_focus_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'FOCUS.EXE')], check=True)
    try:
        plain_log, plain = session(args.kms, drive, False)
        dialog_log, dialog = session(args.kms, drive, True)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(plain_log + dialog_log)
    stops = [line for line in (plain_log + dialog_log).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    checks = {'both runs wrote their results without a stop': not stops and plain and dialog}
    for name, r in (('plain', plain), ('dialog', dialog)):
        inside, end = (r[10:], r[:10]) if r else ((0,) * 10, (0,) * 10)
        checks.update({
            f'{name}: the clicks inside arrive, two in the client area and one on the frame: {inside[1:3]}':
                inside[1:3] == (2, 1),
            f'{name}: the window gets no WM_SETFOCUS and no activation from them: {inside[3:5]}':
                inside[3:5] == (0, 0),
            f'{name}: the edit keeps the focus: kill {inside[5]}, focus {inside[7]}':
                inside[5] == 0 and inside[7] == 1,
            f'{name}: the caret stays after "abc": {inside[8:10]}': inside[8:10] == (3, 3),
            f'{name}: the click after Other activates the window again: {end[4]}': end[4] == 1,
        })
    checks['plain: DefWindowProc gives the activated window the focus'] = bool(plain) and plain[3] == 1 and plain[7] == 0
    checks['dialog: DefDlgProc gives the focus back to its edit'] = bool(dialog) and dialog[7] == 1
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    if failed:
        print('    plain:', plain, '\n    dialog:', dialog)
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'click focus host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
