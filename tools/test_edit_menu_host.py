#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An edit control opens the context menu of Windows.

USER answers WM_CONTEXTMENU of an edit control with menu 3 of its own
resources: Undo, Cut, Copy, Paste, Delete, Select All. The port opened
nothing: the right button and the message ended in the control without an
answer (src/x86_edit_menu.cpp has the rules of the original).

tools/edit_menu_fixture.c shows a plain edit control, a read-only one and one
with a password character and writes down every change of their text, their
selection and the clipboard. On neo98-kms over a private copy of drive C:,
the menu opened with the right button (and once with SHIFT+F10) and an item
chosen with its underlined letter:

  plain      Select All selects the text, Copy puts it on the clipboard,
             Delete removes it, Undo brings it back, Cut takes it, Paste puts
             it in again
  read-only  Select All and Copy work; Cut, Paste and Delete are greyed and
             change nothing
  password   Select All works; Copy and Cut are greyed and the clipboard
             keeps what it had
  keyboard   SHIFT+F10 opens the same menu

    build/analysis-venv/bin/python tools/test_edit_menu_host.py [--no-build] [--keep] [--kms KMS]
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
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/edit-menu'
PLAIN, READ_ONLY, PASSWORD = (50, 22), (50, 62), (50, 102)
LINE = re.compile(r'0=\[(.*?)\] (\d+)-(\d+) 1=\[(.*?)\] (\d+)-(\d+) 2=\[(.*?)\] (\d+)-(\d+) clip=\[(.*?)\] focus=(-?\d+)')


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
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/edit_menu_fixture.c'), '-nostartfiles',
                    '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'EDITMENU.EXE')], check=True)
    steps, clock = ['0:wait=EditMenu', '0:idle=400'], [0.0]

    def choose(place, key, close=False):
        """The menu at `place`, opened with the right button, and the item of the letter `key`.
        A greyed item does not close the menu; ESC does then."""
        clock[0] += 0.3
        steps.extend([f'{clock[0]:.1f}:rclick={place[0]},{place[1]}', f'{clock[0]:.1f}:wait=#32768',
                      f'{clock[0]:.1f}:idle=200', f'{clock[0]:.1f}:key={key:02x}', f'{clock[0]:.1f}:idle=200'])
        if close:
            steps.extend([f'{clock[0]:.1f}:key=1b', f'{clock[0]:.1f}:idle=200'])

    choose(PLAIN, ord('A'))
    steps.append(f'{clock[0]:.1f}:shot={WORK}/menu.png')
    choose(PLAIN, ord('C'))
    choose(PLAIN, ord('D'))
    choose(PLAIN, ord('U'))
    choose(PLAIN, ord('A'))
    choose(PLAIN, ord('T'))
    choose(PLAIN, ord('P'))
    choose(READ_ONLY, ord('A'))
    choose(READ_ONLY, ord('T'), close=True)
    choose(READ_ONLY, ord('D'), close=True)
    choose(READ_ONLY, ord('P'), close=True)
    choose(READ_ONLY, ord('C'))
    choose(PASSWORD, ord('A'))
    choose(PASSWORD, ord('C'), close=True)
    choose(PASSWORD, ord('T'), close=True)
    # SHIFT+F10 in the plain control, whose selection a click has collapsed.
    clock[0] += 0.3
    steps.extend([f'{clock[0]:.1f}:click={PLAIN[0]},{PLAIN[1]}', f'{clock[0]:.1f}:idle=300',
                  f'{clock[0]:.1f}:keydown=10', f'{clock[0]:.1f}:key=79', f'{clock[0]:.1f}:keyup=10',
                  f'{clock[0]:.1f}:wait=#32768', f'{clock[0]:.1f}:idle=200', f'{clock[0]:.1f}:key=41',
                  f'{clock[0]:.1f}:idle=300', f'{clock[0] + 0.3:.1f}:quit=1'])
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\EDITMENU.EXE', '--time-limit', '120'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=180)
        written = win98_c.path('C:\\EDITMENU.TXT', drive)
        record = written.read_text(errors='replace') if written else ''
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    log = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(log)
    (WORK / 'record.txt').write_text(record)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    timed_out = [line for line in log.splitlines() if 'timed out after' in line]
    # Each state: (text, start, end) of the three controls, the clipboard, the focus.
    states = [((m[1], int(m[2]), int(m[3])), (m[4], int(m[5]), int(m[6])), (m[7], int(m[8]), int(m[9])), m[10], int(m[11]))
              for m in LINE.finditer(record)]
    plain = [s[0] for s in states]
    clips = [s[3] for s in states]

    def order(sequence, *wanted):
        """Whether the states of `wanted` appear in `sequence` in that order."""
        at = 0
        for state in sequence:
            if at < len(wanted) and state == wanted[at]:
                at += 1
        return at == len(wanted)

    last = states[-1] if states else (('', 0, 0),) * 3 + ('', -1)
    checks = {
        f'every menu came, without a stop: {timed_out} {stops[:1]}': not timed_out and not stops and bool(states),
        'the right button gives the control the focus': order([s[4] for s in states], -1, 0),
        'plain: Select All, Copy, Delete, Undo, Select All, Cut, Paste, in this order':
            order(plain, ('hello world', 0, 0), ('hello world', 0, 11), ('', 0, 0), ('hello world', 0, 11),
                  ('', 0, 0), ('hello world', 11, 11))
            and order(clips, '', 'hello world'),
        f'read-only: Select All selects, and the text is what it was: {last[1]}':
            order([s[1] for s in states], ('read only', 0, 0), ('read only', 0, 9))
            and all(s[1][0] == 'read only' for s in states),
        'read-only: Copy puts the text on the clipboard, after Cut, Delete and Paste changed nothing':
            order(clips, 'hello world', 'read only'),
        f'password: Select All selects, Copy and Cut are greyed: {last[2]} and the clipboard {last[3]!r}':
            last[2] == ('secret', 0, 6) and last[3] == 'read only',
        f'SHIFT+F10 opens the menu as well: the plain control ends as {last[0]}': last[0] == ('hello world', 0, 11),
        'the picture of the open menu is there': (WORK / 'menu.png').is_file(),
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Picture, record and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'edit menu host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
