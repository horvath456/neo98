#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The status bar of the 16-bit COMMCTRL with the messages of a 16-bit program.

File Manager (16-bit WINFILE.EXE) makes its status bar with CreateStatusWindow
and fills it with SB_SETPARTS and SB_SETTEXT. On the 16-bit side the parts
are an array of 16-bit ints and the texts are far pointers; the status bar of
COMCTL32 got both unchanged, read the parts and the texts at flat addresses,
and File Manager's status bar showed a stray "]" instead of the free space and
the number of files.

tools/status_win16_fixture.asm makes a status bar of two parts and asks it
back with the 16-bit forms of SB_GETTEXT, SB_GETTEXTLENGTH, SB_GETPARTS,
SB_GETRECT and SB_GETBORDERS:

  fixture  the answers are those of the 16-bit COMMCTRL, no word behind an
           array is written, and each part shows its text
  winfile  File Manager's status bar shows text in both of its parts

    build/analysis-venv/bin/python tools/test_win16_status_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import shutil as _shutil
if not _shutil.which('nasm'):  # the fixture; docs/BUILDING.md
    print('SKIP: needs nasm')
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
import neo98  # noqa: E402
import win16_fixture  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

BINARY = ROOT / 'build/native-app/neo98-kms'
WORK = ROOT / 'build/win16-status'
# File Manager's status bar on a 1024x768 desktop: its left part, with the
# free space of the drive, and its right part, with the files.
WINFILE_LEFT, WINFILE_RIGHT = (4, 748, 218, 766), (224, 748, 386, 766)
BLACK = (0, 0, 0)


def dark(picture, box):
    width, _, data = picture
    left, top, right, bottom = box
    return sum(1 for y in range(top, bottom) for x in range(left, right)
               if tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3]) == BLACK)


def session(kms, drive, program, steps, limit):
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(WORK)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    result = subprocess.run([str(kms), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', program,
                             '--time-limit', str(limit)], cwd=ROOT, env=env, capture_output=True, text=True,
                            timeout=limit + 90)
    return result.stdout + result.stderr


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
    win16_fixture.assemble(ROOT / 'tools/status_win16_fixture.asm', WORK / 'STATUS16.EXE', 'STATUS16',
                           neo98.tool_environment(), modules=('KERNEL', 'USER', 'COMMCTRL'))
    shutil.copyfile(WORK / 'STATUS16.EXE', drive / 'STATUS16.EXE')
    try:
        # The fixture names its window "Status16 done" once its file is
        # complete; the pictures wait until the machine is quiet.
        log = session(args.kms, drive, 'C:\\STATUS16.EXE', ['0:wait=Status16|Status16 done', '0:idle=500',
                                                             '0:shot={out}/fixture.png', '0.5:quit=1'], 90)
        written = win98_c.path('C:\\STATUS16.TXT', drive)
        data = written.read_bytes() if written else b''
        log += session(args.kms, drive, 'C:\\WINDOWS\\WINFILE.EXE', ['0:wait=WFS_Frame', '0:idle=500',
                                                                     '0:shot={out}/winfile.png', '0.5:quit=1'], 90)
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)
    (WORK / 'log.txt').write_text(log)
    stops = [line for line in log.splitlines()
             if 'no native provider' in line or 'CRASHREPORT' in line or 'not ported' in line]
    w = struct.unpack_from('<30H', data) if len(data) >= 60 else None
    fixture = read_png(WORK / 'fixture.png') if (WORK / 'fixture.png').is_file() else None
    winfile = read_png(WORK / 'winfile.png') if (WORK / 'winfile.png').is_file() else None
    checks = {'the fixture wrote its answers, both runs without a stop': w is not None and not stops,
              'the fixture said it was done, both pictures waited for a quiet machine':
                  'NEO98 KMS input wait=Status16|Status16 done' in log and log.count('NEO98 KMS input idle=500 reached') == 2}
    if w:
        text = data[12:28].split(b'\0')[0]
        rect = [v - 65536 if v & 0x8000 else v for v in w[20:24]]
        origin = (w[28], w[29])
        checks.update({
            f'fixture: SB_SETPARTS and both SB_SETTEXT succeed: {w[1:4]}': w[1:4] == (1, 1, 1),
            f'fixture: SB_GETTEXT answers the length and the text of part 1: {w[4]}, {text!r}':
                (w[4], w[5]) == (11, 0) and text == b'Second part',
            f'fixture: SB_GETTEXTLENGTH of part 0: {w[14]}': w[14] == 10,
            f'fixture: SB_GETPARTS answers the parts as 16-bit ints: {w[15]}, {w[16:18]}':
                w[15] == 2 and w[16:18] == (150, 0xffff),
            f'fixture: nothing is written behind them: {w[18]:04x}': w[18] == 0xaaaa,
            f'fixture: SB_GETRECT of part 0 in words: {rect}': w[19] == 1 and 100 < rect[2] <= 152 and
                rect[1] >= 0 and 0 < rect[3] - rect[1] < 40,
            f'fixture: SB_GETBORDERS in three words: {w[25:28]}': w[24] == 1 and all(v < 16 for v in w[25:28]),
        })
        if fixture:
            first = (origin[0] + 4, origin[1] + 2, origin[0] + 140, origin[1] + 18)
            second = (origin[0] + 160, origin[1] + 2, origin[0] + 300, origin[1] + 18)
            checks[f'fixture: each part shows its text: {dark(fixture, first)}, {dark(fixture, second)}'] = \
                dark(fixture, first) > 30 and dark(fixture, second) > 30
    if winfile:
        left, right = dark(winfile, WINFILE_LEFT), dark(winfile, WINFILE_RIGHT)
        checks[f'winfile: the status bar shows text in both parts: {left}, {right}'] = left > 60 and right > 60
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    if stops:
        print('    stop:', stops[0])
    if w and failed:
        print('    words:', [hex(v) for v in w])
    print(f'Pictures and log in {WORK}', flush=True)
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'win16 status host: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
