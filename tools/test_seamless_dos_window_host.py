#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Seamless: an MS-DOS program with "full screen" in its PIF gets a window.

LOEWE.PIF (`GAMES/LOEWE`, a DOS game in 320x200, "full screen" set) started
by a double click in the seamless view showed as a picture without a frame
in the top left corner of the Mac: the DOS window drops its frame for full
screen and covers 800x600 at 0,0, which on a desktop as large as the Mac's
screen is a corner. The seamless view has no screen a DOS mode could take;
the host says so (X86::Application::SetHostKeepsDosWindows), and the session
keeps its window, with caption, title, minimize and close buttons, the
picture at twice its size, like every other window of the guest.

Explorer is the shell, the host runs headless with --seamless, the game is
copied to C:\\GAMES\\LOEWE of the test's own C: (where its PIF names it):

- the session reports that it stays in its window, and the overlay stays;
- the Mac shows exactly one window of the game, with the title LOEWE, a
  caption above a client of 640x400, inside the screen and opaque;
- View > Fullscreen shows the game alone over the screen (its client
  area), and Nahtlos stays chosen;
- its close button ends it: full screen ends by itself, the overlay is back,
  no window of the game is left, and the place is transparent again.

    build/analysis-venv/bin/python tools/test_seamless_dos_window_host.py [--no-build]

Skips without the game. Results in build/seamless-dos-window-host/results.json.
"""
from __future__ import annotations

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
from host_test_process import run_logged  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
from programs_drive import COLLECTION  # noqa: E402
from test_seamless_program_mode_host import alpha  # noqa: E402

GAME = COLLECTION / 'GAMES/LOEWE'
SCREEN = (1100, 700)


def main():
    if not (GAME / 'LOEWE.PIF').is_file():
        print(f'SKIP {GAME} is not there')
        raise SystemExit(77)
    out = neo98.output_directory('seamless-dos-window-host')
    for old in list(out.glob('*.pgm')) + list(out.glob('*.log')):
        old.unlink()
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    drive = out / 'drive-c'
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive)], check=True)
    (drive / 'GAMES').mkdir(exist_ok=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(GAME), str(drive / 'GAMES/LOEWE')], check=True)
    settings = out / 'settings'
    settings.mkdir(exist_ok=True)
    (settings / 'settings.ini').write_text('scale = 1\nseamless = 1\n')
    shown, gone = out / 'shown.pgm', out / 'gone.pgm'
    steps = ['0:wait=Shell_TrayWnd', '0:idle=3000', f'0:open={drive / "GAMES/LOEWE/LOEWE.PIF"}', '0:wait=|LOEWE',
             '6:windows=1', f'6:mask={shown}',
             # View > Fullscreen: the game alone on the screen, until it ends.
             # Alt+Enter in the DOS window: the same full screen and back.
             '6.2:keydown=12', '6.3:key=0d', '6.4:keyup=12', '6.8:keydown=12', '6.9:key=0d', '7:keyup=12',
             '7.5:menu=fullscreen', '9:menu=describe', '10:close=LOEWE', '15:windows=1', f'15:mask={gone}',
             '13:menu=quit']
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports'), '--seamless']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_SETTINGS_DIR=str(settings),
               NEO98_HOST_SCREEN='%dx%d' % SCREEN, NEO98_HOST_DEADLINE='540')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    log_path = out / 'host.log'
    code, text = run_logged(command, log_path, cwd=ROOT, env=env, timeout=600)
    shutil.rmtree(drive, ignore_errors=True)
    shutil.rmtree(out / 'reports', ignore_errors=True)
    waits = re.findall(r'^NEO98 HOST input wait=(\S.*) (reached|timed out) after', text, re.M)
    overlays = re.findall(r'^NEO98 HOST seamless overlay=(\d)', text, re.M)
    lists, current = [], None
    for line in text.splitlines():
        if line.startswith('NEO98 HOST seamless items '):
            current = []
            lists.append(current)
        elif line.startswith('NEO98 HOST seamless item ') and current is not None:
            match = re.search(r'frame=(-?\d+),(-?\d+),(-?\d+),(-?\d+) .* title=(.*)$', line)
            current.append((tuple(int(v) for v in match.groups()[:4]), match.group(5)))
    game = [frame for frame, title in lists[0] if title.startswith('LOEWE')] if lists else []
    checks = {
        'host ended': code == 0,
        'every wait reached': bool(waits) and all(w[1] == 'reached' for w in waits),
        'the PIF asks for full screen': re.search(r'^DOSVM start .* fullscreen=1 ', text, re.M) is not None,
        'the session keeps its window': 'DOSVM fullscreen=0 (the host keeps DOS sessions in their windows)' in text,
        'the overlay stays until full screen is chosen': overlays[:1] == ['1'] and '(program mode)' not in text
            and 'window covers the screen' not in text,
        'full screen shows the window of the game alone':
            re.search(r'^NEO98 HOST full screen of a program shows \d+,\d+,\d+x\d+ of hwnd=', text, re.M) is not None
            and 'NEO98 HOST seamless overlay=0' in text and '(full screen of a program)' in text,
        'full screen shows the DOS picture without the toolbar':
            re.search(r'^NEO98 HOST full screen of a program shows \d+,\d+,640x400 of hwnd=', text, re.M) is not None,
        'Alt+Enter takes the DOS window to full screen and back':
            '(asked by the DOS window)' in text and 'NEO98 HOST full screen of a program ends (the DOS window asked)' in text,
        'Nahtlos stays chosen in full screen': 'NEO98 HOST seamless off' not in text
            and re.search(r'menu-item View > Seamless \[seamless\].*\(enabled\)', text) is not None
            and re.search(r'menu-item View > Fullscreen \[fullscreen\].*\(enabled\)', text) is None,
        'the game ends: full screen ends and the overlay is back by itself':
            "NEO98 HOST full screen of a program ends (the program's window is gone)" in text
            and overlays == ['1', '0', '1', '0', '1'],
        'exactly one Mac window of the game': len(game) == 1,
    }
    if len(game) == 1 and shown.exists() and gone.exists():
        left, top, right, bottom = game[0]
        width, height = right - left, bottom - top
        middle = ((left + right) // 2, (top + bottom) // 2)
        # The mask can precede the game's graphics switch. Auto WOAFont
        # then fits an 80x25 text grid with 12x17 cells to this 1100x700
        # screen. Accept that exact client only while that text mode and
        # PIF Auto-font choice are actually active at the captured mask.
        captured = re.search(r'NEO98 HOST seamless item[^\n]*title=LOEWE', text)
        graphics = re.search(r'Resolution changed 640x400[^\n]*=> 320x200', text)
        font = re.search(r'DOSVM misc[^\n]*font=([0-9a-f]+),', text)
        auto_text = (captured is not None and graphics is not None and captured.start() < graphics.start()
                     and font is not None and int(font[1], 16) & 0x10
                     and re.search(r'DOSVM first frame[^\n]*size=640x400', text) is not None)
        framed = 640 < width <= 660 and 300 < height <= 520
        auto_framed = auto_text and 960 < width <= 980 and 425 < height <= 465
        checks['a caption and a frame around the picture'] = framed or auto_framed
        checks['inside the screen'] = left >= 0 and top >= 0 and right <= SCREEN[0] and bottom <= SCREEN[1]
        checks['opaque in caption and picture'] = (alpha(shown)[2](*middle) == 255
                                                   and alpha(shown)[2](middle[0], top + 10) == 255)
        checks['closed, no window of the game is left'] = (
            len(lists) > 1 and not any(title.startswith('LOEWE') or 'LOEWE' in title for _, title in lists[1]))
        # Where the game stood and no other window stands.
        others = lists[1] if len(lists) > 1 else []
        free = next(((x, y) for x in range(left + 5, right, 20) for y in range(top + 5, bottom, 20)
                     if not any(f[0] <= x < f[2] and f[1] <= y < f[3] for f, _ in others)), None)
        checks['and its place is transparent again'] = free is not None and alpha(gone)[2](*free) == 0
    (out / 'results.json').write_text(json.dumps(checks, indent=1))
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'SEAMLESS DOS WINDOW FAIL; see {log_path}')
        return 1
    print('SEAMLESS DOS WINDOW PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
