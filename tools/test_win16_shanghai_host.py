#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Start Shanghai II from Explorer and check intro, music, menu and new game.

Its PeekMessage title loop must count as progress without a host hang warning. A menu press must reach the guest queue before menu tracking so the board is initialized. MCI_STOP must deliver an aborted notification promptly, and MCI_SEEK without a parameter block must rewind music so audio continues past the first loop. Results: build/win16-shanghai-host/results.json."""
from __future__ import annotations

import array
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from programs_drive import GAMES  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

RATE = 44100


def pixels(path):
    header, size, maximum, body = path.read_bytes().split(b'\n', 3)
    width, height = map(int, size.split())
    assert header == b'P6' and len(body) == width * height * 3
    return width, height, body


def colours(shot, box):
    """The distinct colours in a box of a screenshot, every second pixel."""
    width, _, body = shot
    left, top, right, bottom = box
    return {body[(y * width + x) * 3:(y * width + x) * 3 + 3]
            for y in range(top, bottom, 2) for x in range(left, right, 2)}


def loud_seconds(tap):
    """Per second of the tap: whether the device played anything but silence."""
    samples = array.array('h')
    samples.frombytes(tap.read_bytes()[: tap.stat().st_size // 4 * 4])
    step = RATE * 2
    return [max((abs(v) for v in samples[at:at + step:16]), default=0) > 200
            for at in range(0, len(samples) - step + 1, step)]


def main():
    from test_support import require_file
    from external_paths import external_path
    require_file(external_path('NEO98_COLLECTION') / 'GAMES/SHANGHAI')
    out = neo98.output_directory('win16-shanghai-host')
    drive_c, drive_d = out / 'drive-c', out / 'drive-d'
    for folder in (drive_c, drive_d):
        shutil.rmtree(folder, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    # The game writes into its folder; it plays on a private copy, with the
    # intro its configuration ships with.
    game = drive_d / 'GAMES' / 'SHANGHAI'
    shutil.copytree(GAMES / 'SHANGHAI', game)
    for entry in game.iterdir():
        if win98_c.metadata_name(entry.name):
            entry.unlink()
    ini = game / 'DEWIN.INI'
    ini.write_bytes(ini.read_bytes().replace(b'title=0', b'title=1'))
    shots = {name: out / f'{name}.ppm' for name in ('title', 'title-late', 'menu', 'dialog', 'board', 'late')}
    for shot in shots.values():
        shot.unlink(missing_ok=True)
    tap = out / 'sound.tap'
    tap.unlink(missing_ok=True)
    shutil.rmtree(out / 'reports', ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # The frame of the game stands at 65,43 on the 800x600 desktop; the File menu
    # is the first item of its menu bar and "Neues Spiel..." the first of the
    # popup. The clock of the script stands during a wait.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=1500',
             '1:guestopen=D:\\GAMES\\SHANGHAI\\SHANGHAI.EXE', '1:wait=sh2frame',
             f'4:shot={shots["title"]}', f'12:shot={shots["title-late"]}',
             '12.5:click=90,75', '12.5:wait=#32768', f'13.5:shot={shots["menu"]}',
             '14:click=120,96', '14:wait=#32770|Neues Spiel', f'15:shot={shots["dialog"]}',
             '15.5:key=0d', f'18.5:shot={shots["board"]}',
             f'47:shot={shots["late"]}', '48:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_STATS='1',
               NEO98_AUDIO_TAP=str(tap), NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_TRACE_CALLS_FROM'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry), '--drive', f'D={drive_d}',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=900)
    for folder in (drive_c, drive_d):
        shutil.rmtree(folder, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    taken = {name: pixels(shot) if shot.exists() else None for name, shot in shots.items()}
    client = (72, 92, 728, 548)  # inside the client area of the game's window
    numbers = {}

    def count(name, box=client):
        numbers[name] = len(colours(taken[name], box)) if taken[name] else 0
        return numbers[name]

    # The title is a painted dragon on its own dark background; the
    # board is tiles on one plain blue. The corner at the lower right is empty
    # in both and tells them apart.
    corner = (690, 515, 726, 546)
    title_corner = colours(taken['title'], corner) if taken['title'] else set()
    board_corner = colours(taken['board'], corner) if taken['board'] else set()
    menu_corner = colours(taken['menu'], corner) if taken['menu'] else set()
    reports = out / 'reports'

    def same(first, second, box=client):
        if not taken[first] or not taken[second]:
            return False
        width, left, top, right, bottom = taken[first][0], *box
        return all(taken[first][2][(y * width + left) * 3:(y * width + right) * 3] ==
                   taken[second][2][(y * width + left) * 3:(y * width + right) * 3] for y in range(top, bottom))

    loud = loud_seconds(tap) if tap.exists() else []
    numbers['tap seconds'] = len(loud)
    numbers['loud seconds'] = sum(loud)
    numbers['loud in the last five seconds'] = sum(loud[-6:-1])
    hangs = len(re.findall(r'NEO98 HOST hang begin', text))
    numbers['hang reports'] = hangs
    checks = {
        'game window open': 'input wait=sh2frame reached' in text,
        'title picture painted': count('title') >= 60 and len(title_corner) == 1,
        'title still up eight seconds later': same('title', 'title-late'),
        'no hang reported while the title polls': hangs == 0,
        'File menu popup dropped': 'input wait=#32768 reached' in text,
        'the press ended the title, the board lies behind the menu':
            len(menu_corner) == 1 and menu_corner != title_corner and count('menu') > 20,
        'dialog Neues Spiel opened': 'input wait=#32770|Neues Spiel reached' in text,
        'board dealt three seconds after OK': len(board_corner) == 1 and board_corner == menu_corner
            and count('board') > 20 and not same('board', 'menu', (72, 250, 728, 548)),
        'no report, and nothing over the board half a minute later':
            not (reports.is_dir() and any(reports.iterdir())) and same('board', 'late'),
        'sound from the first second of the game': len(loud) > 40 and any(loud[:len(loud) - 40]),
        'music repeats: sound 30 seconds after the new game': sum(loud[-6:-1]) >= 4,
        'screenshots written': all(shot.exists() for shot in shots.values()),
        'host ended': result.returncode == 0,
    }
    (out / 'results.json').write_text(json.dumps({'steps': steps, 'checks': checks, 'numbers': numbers},
                                                 indent=2, ensure_ascii=False) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(', '.join(f'{name}: {value}' for name, value in numbers.items()))
    if not all(checks.values()):
        print(f'SHANGHAI HOST FAIL: see {log_path}')
        return 1
    print(f'SHANGHAI HOST PASS: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
