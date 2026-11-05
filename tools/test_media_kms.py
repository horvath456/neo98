#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Media Center 98 in neo98-kms with Kodi under NEO98's compositor.

No real Kodi: NEO98_KODI names tools/test_kodi_server.py, which the host
starts like Kodi, with --windowing=wayland and WAYLAND_DISPLAY of its
compositor; started that way it runs the Wayland test client
(tools/test_wayland_client.c) as Kodi's window, a picture of one colour.
The picture stays in memory (NEO98_KMS_OUTPUT=image:1280x720): the guest's
800x600 at 160,0 scaled by 1.2. No sound, no DISPLAY.

  window the host starts Kodi under its compositor; the episode plays in
          Media Center's video area, where the program paints the key
          colour; an open menu covers it; dragging the window takes the
          video along; a double click gives it the whole screen, Esc goes
          back to the window; Stop shows the list again; at least 57 of
          Kodi's 60 pictures a second reach the screen (NEO98_MEDIA_FPS)

    build/analysis-venv/bin/python tools/test_media_kms.py [--no-build] [--only PART...]
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_compositor_host import CLIENT, build_client  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

PARTS = ('window',)
COLOUR = 0x10c0c0
# Netflix > TV Shows > Stranger Things > Season 1 > the first episode, in
# the large icon view at its default place (guest coordinates).
BROWSE = ['3:dblclick=318,170', '5:dblclick=443,170', '7:dblclick=318,170', '9:dblclick=318,170']


def screen(gx, gy):
    return int(160 + gx * 1.2), int(gy * 1.2)


def coloured(picture, box=None):
    """(count, left, top, right, bottom) of the client's colour in the picture or a box of it."""
    width, height, data = picture
    x0, y0, x1, y1 = box or (0, 0, width, height)
    count, left, top, right, bottom = 0, width, height, -1, -1
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(x0, x1):
            i = row + x * 3
            if data[i] == 0x10 and data[i + 1] == 0xc0 and data[i + 2] == 0xc0:
                count += 1
                left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
    return count, left, top, right, bottom


def run(out, name, steps):
    folder = out / name
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    held = socket.socket()
    held.bind(('127.0.0.1', 0))
    port = held.getsockname()[1]
    held.close()
    helper = socket.socket()
    helper.bind(('127.0.0.1', 0))
    helper_port = helper.getsockname()[1]
    helper.close()
    # Nothing of a real Kodi: the test Kodi on ports of its own, no folder
    # of the user's Kodi to put the add-on in.
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KMS_OUTPUT='image:1280x720', NEO98_KEYMAP='US',
               NEO98_KODI=str(ROOT / 'tools/test_kodi_server.py'), NEO98_KODI_PORT=str(port),
               NEO98_KODI_HELPER_PORT=str(helper_port), NEO98_KODI_EVENTS_PORT='9', NEO98_KODI_HOME=str(folder / 'no-kodi-home'),
               NEO98_TEST_KODI_LOG=str(folder / 'calls.jsonl'), NEO98_TEST_WAYLAND_CLIENT=str(CLIENT),
               NEO98_TEST_WAYLAND_COLOUR='%06x' % COLOUR, NEO98_MEDIA_FPS='1', NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(folder)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_TEST_KODI', 'NEO98_TEST_COMPOSITOR'):
        env.pop(variable, None)
    (folder / 'calls.jsonl').unlink(missing_ok=True)
    result = subprocess.run([str(ROOT / 'build/native-app/neo98-kms'), '--ram-drive', '--no-registry-file', '--exe',
                             r'C:\WINDOWS\MEDIACTR.EXE', '--time-limit', '60'], cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
    (folder / 'log.txt').write_text(result.stdout + result.stderr)
    pictures = {p.stem: read_png(p) for p in folder.glob('*.png')}
    return result.returncode, result.stdout + result.stderr, pictures


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', nargs='+', choices=PARTS)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host(host_media=True)
    build_client()
    pristine = win98_c.use_pristine()
    out = ROOT / 'build/media-kms'
    out.mkdir(parents=True, exist_ok=True)
    results, failed = {}, []

    def check(name, condition):
        results[name] = bool(condition)
        print(('PASS ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    for part in args.only or PARTS:
        if part == 'window':
            steps = ['0:wait=NEO98MediaCenter', *BROWSE, '11:dblclick=318,170', '15:shot={out}/window.png',
                     '15.5:click=161,87', '16.5:shot={out}/menu.png', '17:key=1b', '17.5:key=1b', '18:drag=300,67,260,97',
                     '19.5:shot={out}/moved.png', '20:dblclick=470,300', '22:shot={out}/whole.png', '23:key=1b',
                     '24.5:shot={out}/back.png', '25:ctrl=53', '27:shot={out}/stopped.png', '28:quit=1']
            code, text, pictures = run(out, part, steps)
            check('window: the host starts Kodi under its compositor',
                  "starting Kodi: " in text and "under NEO98's compositor" in text and 'compositor client connected' in text)
            check('window: the video plays inside the window', 'NEO98 HOST media screen window' in text and 'screen kodi' not in text)
            window = coloured(pictures['window']) if 'window' in pictures else (0, 0, 0, 0, 0)
            check(f'window: the video shows in the video area only ({window})',
                  window[0] > 100000 and window[0] < 1280 * 720 // 2 and window[1] > screen(200, 0)[0])
            check('window: the tree beside it shows no video',
                  'window' in pictures and coloured(pictures['window'], (*screen(10, 100), *screen(window[1] / 1.2 - 140, 400)))[0] == 0)
            if 'menu' in pictures and window[0]:
                # The Play menu drops down over the video's top left corner.
                gx, gy = (window[1] - 160) / 1.2, window[2] / 1.2
                under = coloured(pictures['menu'], (*screen(gx + 2, gy + 4), *screen(gx + 20, gy + 14)))[0]
                beside = coloured(pictures['menu'], (*screen(gx + 250, gy + 4), *screen(gx + 270, gy + 14)))[0]
                check(f'window: an open menu covers the video ({under} under it, {beside} beside it)', under == 0 and beside > 100)
            else:
                check('window: the menu picture was taken', False)
            moved = coloured(pictures['moved']) if 'moved' in pictures else (0, 0, 0, 0, 0)
            check(f'window: the video follows the window, 48 left and 36 down ({window[1:]} to {moved[1:]})',
                  abs(moved[1] - (window[1] - 48)) <= 2 and abs(moved[4] - (window[4] + 36)) <= 2)
            whole = coloured(pictures['whole'])[0] if 'whole' in pictures else 0
            check(f'window: a double click gives the video the whole screen ({whole})',
                  'NEO98 HOST media full screen on' in text and whole >= 1280 * 720 * 99 // 100)
            back = coloured(pictures['back']) if 'back' in pictures else (0, 0, 0, 0, 0)
            check(f'window: Esc goes back to the window ({back[1:]})',
                  'NEO98 HOST media full screen off' in text and abs(back[1] - moved[1]) <= 2 and abs(back[4] - moved[4]) <= 2)
            # Kodi's test window commits a picture on each frame callback, 60 a second.
            rates = sorted(int(l.split('=')[1]) for l in text.splitlines() if l.startswith('NEO98 HOST video fps='))
            median = rates[len(rates) // 2] if rates else 0
            check(f'window: Kodi\'s pictures reach the screen at least 57 times a second ({median} in the middle of {rates})', median >= 57)
            check('window: Stop shows the list again', 'stopped' in pictures and coloured(pictures['stopped'])[0] == 0)
            check('window: the host ends cleanly and Kodi with it', code == 0 and 'compositor client left' in text)
    assert not win98_c.pristine_problems(pristine), 'The test changed the pristine C:'
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    if failed:
        raise SystemExit(f'MEDIA KMS FAIL: {len(failed)} of {len(results)}, see {out}')
    print(f'MEDIA KMS PASS: {len(results)} checks')


if __name__ == '__main__':
    main()
