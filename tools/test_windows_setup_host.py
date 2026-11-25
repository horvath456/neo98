#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Add/Remove Programs of Windows 98: the pages Windows Setup and Startup Disk.

APPWIZ.CPL takes both pages from the 16-bit SETUPX.DLL, through SHELL.DLL
and COMCTL32's proxy pages. Windows Setup lists the components with the
space they take and the free space of the drive, which SETUPX asks in real
mode: a routine of its own that calls INT 21h AX=7303h (from DOS 7.10 on) or
AH=36h and answers the free sectors in DX:AX. That routine was not ported,
and the run stopped when the page opened.

On neo98-kms over a private copy of drive C:, with the picture of a 1024x768
desktop in memory: the sheet opens, a click shows Windows Setup, which fills
its list of components, and a click shows Startup Disk; neither stops the
run.

    build/analysis-venv/bin/python tools/test_windows_setup_host.py [--no-build] [--keep] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
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
WORK = ROOT / 'build/windows-setup'
# The tabs of the sheet on a 1024x768 desktop, and its page area.
SETUP_TAB, DISK_TAB = (203, 95), (280, 95)
PAGE = (80, 110, 420, 470)


def area(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return [bytes(data[(y * width + left) * 3:(y * width + right) * 3]) for y in range(top, bottom)]


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
    # Windows Setup takes a while to list the components.
    steps = ['0:wait=#32770', '4:shot={out}/install.png', f'5:click={SETUP_TAB[0]},{SETUP_TAB[1]}',
             '40:shot={out}/setup.png', f'41:click={DISK_TAB[0]},{DISK_TAB[1]}', '50:shot={out}/disk.png',
             '52:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    try:
        result = subprocess.run([str(args.kms), '--drive-c', str(drive), '--desktop', '1024x768',
                                 '--exe', 'C:\\WINDOWS\\RUNDLL32.EXE',
                                 '--command', 'rundll32.exe shell32.dll,Control_RunDLL appwiz.cpl',
                                 '--time-limit', '80'], cwd=ROOT, env=env, capture_output=True, text=True,
                                timeout=170)
    finally:
        if not args.keep:
            shutil.rmtree(drive, ignore_errors=True)
            win98_c.journal(drive).unlink(missing_ok=True)
    text = result.stdout + result.stderr
    (WORK / 'log.txt').write_text(text)
    stops = [line for line in text.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    titles = re.findall(r'WIN16 proxy page info record=\w+ size=\d+x\d+ title=\[(.*?)\]', text)
    pictures = {name: read_png(WORK / f'{name}.png') if (WORK / f'{name}.png').is_file() else None
                for name in ('install', 'setup', 'disk')}
    pages = {name: area(picture, PAGE) for name, picture in pictures.items() if picture}
    checks = {
        f'SETUPX gives the pages Windows Setup and Startup Disk: {titles}':
            {'Windows Setup', 'Startup Disk'} <= set(titles),
        'Windows Setup and Startup Disk open without a stop': not stops and len(pages) == 3,
        'each page shows something of its own':
            len(pages) == 3 and len({b''.join(page) for page in pages.values()}) == 3,
    }
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'windows setup host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
