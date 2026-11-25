#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""16-bit property sheet pages in the 32-bit sheets of Windows 98.

Display Properties, page Settings, Advanced... opens the sheet of the display
adapter. DESK.CPL takes three of its pages from the 16-bit DESKCP16.DLL
("DESKCP16.DLL,GetAdapterPage", GetMonitorPage, GetPerformancePage): SHELL32
hands each function to SHELL.DLL (flat thunk index 10), which calls it with
its own collector of pages, and COMCTL32 wraps every 16-bit page in a proxy
page that asks COMMCTRL for its size, title and dialog. Before, the thunk
answered no pages and the sheet had only General and Color Management.

The MS-DOS program pages of PIFMGR take the same thunk and keep their own
path (tools/msdos_properties_fixture.c opens them for COMMAND.COM).

On neo98-kms over a private copy of drive C:, with the picture of a 1024x768
desktop in memory:

  pages    the adapter's sheet has General, Adapter, Monitor, Performance and
           Color Management; each 16-bit page is made as a dialog, and
           Ctrl+Tab shows five different pages without a stop
  msdos    the properties of COMMAND.COM show the pages of an MS-DOS
           program; Ctrl+Tab goes from General to Program and Font

The steps wait for each sheet and for a quiet machine, not for a time.

    build/analysis-venv/bin/python tools/test_display_advanced_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture of part msdos; docs/BUILDING.md
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
WORK = ROOT / 'build/display-advanced'
# Advanced... on the page Settings, and the page area of the sheet of the
# adapter that opens over Display Properties.
ADVANCED = (403, 441)
PAGE = (90, 120, 470, 475)
PAGES = 5
# The page area of the sheet of COMMAND.COM, below its two rows of tabs.
MSDOS_PAGE = (75, 125, 420, 455)


def area(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return [bytes(data[(y * width + left) * 3:(y * width + right) * 3]) for y in range(top, bottom)]


def session(kms, drive, program, command, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    line = [str(kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', program,
            '--time-limit', str(limit)]
    if command:
        line += ['--command', command]
    result = subprocess.run(line, cwd=ROOT, env=env, capture_output=True, text=True, timeout=limit + 90)
    return result.stdout + result.stderr


def walk(name, count):
    """Steps that show `count` pages one after the other with Ctrl+Tab, each
    picture once the machine is quiet."""
    steps = ['0:idle=1000', f'0:shot={{out}}/{name}0.png']
    for page in range(1, count):
        steps += [f'{0.3 * page}:ctrl=09', f'{0.3 * page}:idle=500', f'{0.3 * page}:shot={{out}}/{name}{page}.png']
    return steps + [f'{0.3 * count}:quit=1']


def pages(name, count, box):
    pictures = [read_png(WORK / f'{name}{n}.png') if (WORK / f'{name}{n}.png').is_file() else None
                for n in range(count)]
    return [area(picture, box) if picture else None for picture in pictures]


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
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools/msdos_properties_fixture.c'), '-nostartfiles',
                    '-Wl,-e,_mainCRTStartup', '-mwindows', '-lshell32', '-luser32', '-lkernel32',
                    '-o', str(drive / 'PROPS.EXE')], check=True)
    try:
        # Each step waits for its sheet or for a quiet machine.
        text = session(args.kms, drive, 'C:\\WINDOWS\\RUNDLL32.EXE',
                       'rundll32.exe shell32.dll,Control_RunDLL desk.cpl,,3',
                       ['0:wait=#32770|Display Properties', '0:idle=1000', f'0:click={ADVANCED[0]},{ADVANCED[1]}',
                        '0:wait=#32770|NEO98 Display Adapter Properties', *walk('page', PAGES)], 120)
        msdos = session(args.kms, drive, 'C:\\PROPS.EXE', None, ['0:wait=#32770', *walk('msdos', 3)], 120)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(text + msdos)
    stops = [line for line in (text + msdos).splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    timed_out = [line for line in (text + msdos).splitlines() if 'timed out after' in line]
    titles = re.findall(r'WIN16 proxy page info record=\w+ size=\d+x\d+ title=\[(.*?)\]', text)
    dialogs = re.findall(r'WIN16 proxy page dialog record=\w+ parent=\w+ procedure=\w+ hwnd=(\w+)', text)
    shown = pages('page', PAGES, PAGE)
    distinct = len({b''.join(page) for page in shown if page})
    collected = re.findall(r'FLATTHUNK dispatched index=10 .*? result=(\w+)', msdos)
    msdos_shown = pages('msdos', 3, MSDOS_PAGE)
    msdos_distinct = len({b''.join(page) for page in msdos_shown if page})
    checks = {
        f'every sheet and quiet moment came: {timed_out}': not timed_out,
        'both sheets open without a stop': not stops and all(shown) and all(msdos_shown),
        f'pages: Adapter, Monitor and Performance of DESKCP16 are in the sheet: {titles}':
            {'Adapter', 'Monitor', 'Performance'} <= set(titles),
        f'pages: each 16-bit page is made as a dialog: {dialogs}':
            len(dialogs) >= 3 and all(int(hwnd, 16) for hwnd in dialogs),
        f'pages: Ctrl+Tab shows five different pages: {distinct}': distinct == 5,
        f'msdos: SHELL.DLL collects the pages of PIFMGR: {collected}':
            bool(collected) and int(collected[0], 16) == 1,
        f'msdos: General, Program and Font are three different pages: {msdos_distinct}': msdos_distinct == 3,
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'display advanced host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
