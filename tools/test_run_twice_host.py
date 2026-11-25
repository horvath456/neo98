#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Start > Run twice in a row, on neo98-kms.

When the Run box closes, the autocomplete of its edit field (BROWSEUI)
posts 402h and WM_QUIT to its own thread and waits for that thread to end,
three seconds and then fifteen. The thread takes each message, drains the
further 402h with PeekMessage(402h..402h, PM_REMOVE) and ends at WM_QUIT.
PeekMessage handed a posted WM_QUIT to every filter, so the drain took it,
the thread never ended, and the thread of the Run box stood eighteen
seconds in WM_DESTROY. Then it gave the activation to the desktop: a Run
box opened in the meantime lost it, and what was typed into it went to
the desktop's icons.

tools/run_twice_fixture.c, started as "C:\\RUNTWICE.EXE 1" from the first
Run box, checks the filter itself (a range, a window, a thread waiting as
the autocomplete does) before its window shows; at once the second Run box
starts "C:\\RUNTWICE.EXE 2", whose window has to show.

    build/analysis-venv/bin/python tools/test_run_twice_host.py [--no-build] [--kms KMS]
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
WORK = ROOT / 'build/run-twice'
START = (25, 755)


def run_box(command):
    """Start > Run, the command typed into the box and Enter."""
    return [f'0:click={START[0]},{START[1]}', '0:wait=BaseBar', '0.5:type=r', '0.5:wait=#32770|Run',
            '0.5:idle=300', f'1:type={command}', '1.5:key=0d']


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/run_twice_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-luser32', '-lkernel32',
                    '-o', str(drive / 'RUNTWICE.EXE')], check=True)
    # The second Run box opens as soon as the first program shows, while
    # the thread of the first box may still be closing it.
    steps = (['0:wait=Shell_TrayWnd', '0:idle=500'] + run_box('C:\\RUNTWICE.EXE 1') +
             ['1.5:wait=RunTwice|Run twice 1'] + run_box('C:\\RUNTWICE.EXE 2') +
             ['1.5:wait=RunTwice|Run twice 2', '1.5:quit=1'])
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe',
                                 'C:\\WINDOWS\\EXPLORER.EXE', '--time-limit', '120'], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=180)
        text = result.stdout + result.stderr
        answer = win98_c.path('C:\\RUNTWICE.TXT', drive)
        lines = answer.read_text(errors='replace').split() if answer else []
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(text)
    got = dict(line.split('=', 1) for line in lines if '=' in line)
    timed_out = [line for line in text.splitlines() if 'timed out after' in line]
    checks = {
        f'range: PeekMessage(402h..402h) leaves a posted WM_QUIT in the queue: {got.get("range")}':
            got.get('range') == '1',
        f'window: a WM_QUIT posted to the thread does not come to a PeekMessage for a window: {got.get("window")}':
            got.get('window') == '1',
        f'thread: a thread that drains 402h as the autocomplete does ends at WM_QUIT: {got.get("thread")}':
            got.get('thread') == '1',
        'second: the second Run box starts its program': 'NEO98 KMS input wait=RunTwice|Run twice 2\n' in text,
        f'every menu, box and window came: {timed_out}': not timed_out,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Log in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'run twice: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
