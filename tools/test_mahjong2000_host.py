#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""MahJong 2000 Luxury Edition in the host: the score box, the click response
 and the board after "New game".

`GAMES/ALTEPC/Spiele/MAHJONK/MJ_LUX11.EXE` is a Delphi program. Right of its
status line stands a panel with two TStaticText controls, "Score:" and the
score. TStaticText superclasses STATIC and forwards every message with
CallWindowProc to the class procedure GetClassInfo named. STATIC shared its
procedure token with the dialog class, so the forwarded messages went to the
dialog procedure, the window was never a static control and only its
background appeared: a dark green bar. STATIC now has a token of its own.

A click on a tile redraws the board through memory DCs; every blit into a
device-dependent bitmap copied its bits word by word through the bitmap
store. The selection showed about 1.3 seconds after the click. The copy now
moves the block at once.

The script clicks the free tile at the left end of the board after the
start. Checks: the score box shows "Score:" and the digits, and the clicked
tile changes within 0.6 s.

Game > New game then shows the "Game Results" dialog (CS_SAVEBITS) over the
board. The game draws the board through GetDC and only erases in WM_PAINT.
USER had dropped the bits saved under the dialog, so its rectangle became
invalid when it closed and stayed in the green of the form over the newly
dealt tiles until a click redrew the board. Checks: after the dialog no
rectangle in the colour of the form lies on the board, and the picture is the
one a click draws (tools/test_savebits_restore_host.py has the same without
the game).

    build/analysis-venv/bin/python tools/test_mahjong2000_host.py [--no-build]
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
from programs_drive import GAMES  # noqa: E402

SOURCE = GAMES / 'ALTEPC/Spiele/MAHJONK'


def pixels(path):
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def at(image, x, y):
    body, width = image
    i = (y * width + x) * 3
    return body[i:i + 3]


def main():
    if not (SOURCE / 'MJ_LUX11.EXE').exists():
        print(f'SKIP {SOURCE} is not there')
        return 0
    out = neo98.output_directory('mahjong2000-host')
    drive_d = out / 'drive-d'
    shutil.rmtree(drive_d, ignore_errors=True)
    (drive_d / 'MAHJONK').mkdir(parents=True)
    for name in ('MJ_LUX11.EXE', 'MJ_RES10.DLL'):
        shutil.copy2(SOURCE / name, drive_d / 'MAHJONK' / name)
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    shots = {name: out / f'{name}.ppm' for name in ('before', 'after', 'dealt', 'redrawn')}
    for path in shots.values():
        path.unlink(missing_ok=True)
    # The board is dealt within five seconds. The tile at the left end of the
    # middle row (62,258) is always free.
    steps = [f'8:shot={shots["before"]}', '8.5:click=62,258', f'9.1:shot={shots["after"]}',
             # Game > New game, Enter on "Abandon current game and start a new
             # one?", OK in "Game Results" (35,73..455,332). The new board is
             # dealt within three seconds; then the same tile is clicked.
             '10:click=27,33', '11:key=4E', '12.5:key=0D', '0:wait=TResDlg1', '13.5:click=245,307',
             f'18.5:shot={shots["dealt"]}', '19:click=62,258', f'20:shot={shots["redrawn"]}', '20.5:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='240')
    env.pop('NEO98_HOST_ATTACHED', None)
    env.pop('NEO98_HOST_MENU', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'D:\\MAHJONK\\MJ_LUX11.EXE', '--quiet',
               '--drive-c', str(drive_c), '--drive', f'D={drive_d}', '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    shutil.rmtree(drive_c, ignore_errors=True)
    checks = {'screenshots written': all(path.exists() for path in shots.values()),
              'host ended': result.returncode == 0}
    if checks['screenshots written']:
        before, after = pixels(shots['before']), pixels(shots['after'])
        # The score box: "Score:" in black on grey (460..500 x 472..488),
        # then the digits in the green field of the second static
        # (504..530 x 474..488).
        label = sum(at(before, x, y) == b'\x00\x00\x00' for x in range(460, 500) for y in range(472, 488))
        digits = sum(at(before, x, y) not in (b'\x00\x80\x00', b'\xc0\xc0\xc0', b'\xff\xff\xff', b'\x80\x80\x80')
                     for x in range(504, 530) for y in range(474, 488))
        checks['score box shows "Score:"'] = label >= 10
        checks['score box shows digits'] = digits >= 5
        changed = sum(at(before, x, y) != at(after, x, y) for x in range(45, 80) for y in range(235, 280))
        checks['clicked tile changes within 0.6 s'] = changed >= 400
        dealt, redrawn = pixels(shots['dealt']), pixels(shots['redrawn'])
        # The part of the dialog's rectangle on the board (the dark field and
        # the tiles); the form itself is green as at 600,300.
        form = at(dealt, 600, 300)
        area = [(x, y) for x in range(40, 450) for y in range(78, 328)]
        checks['new game: no rectangle in the colour of the form on the board'] = \
            sum(at(dealt, x, y) == form for x, y in area) < 500
        checks['new game: the board is the one a click draws'] = \
            sum(at(dealt, x, y) != at(redrawn, x, y) for x, y in area if not (40 <= x < 85 and 230 <= y < 285)) < 200
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'MAHJONG 2000 FAIL; see {log_path}')
        return 1
    print('MAHJONG 2000 PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
