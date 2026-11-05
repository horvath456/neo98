#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Mac pointer is free again after Kroko-Mania ends.

Kroko-Mania 3D (`GAMES/KROKO3D/krok_adventure.exe`, DirectDraw with a mode
change, DINPUT8) recentres the Win98 pointer with SetCursorPos after every
movement, and the host moves the Mac pointer with it (SDL_WarpMouseInWindow,
). Over the guest area the Mac pointer is hidden. SDL3, which runs the
host under sdl2-compat, turns two warps of a hidden pointer to the window
centre within 30 ms into relative mouse mode by itself
(SDL_HINT_MOUSE_EMULATE_WARP_WITH_RELATIVE, on by default) and leaves it only
through SDL_ShowCursor. The host shows the pointer only when it leaves the
guest area, which a pointer in relative mode never does: after the game ended
the Mac pointer stayed caught in the window and the menu bar could not be
reached. The host now switches the hint off before SDL_Init.

The run under SDL_VIDEODRIVER=dummy moves the hand pointer with SDL motion
events (NEO98_HOST_INPUT motion=), fast first, then onto "Exit" in the
game's menu, and clicks it. NEO98_POINTER_TRACE=1 reports the pointer state.
The dummy driver has no relative mode, so the test checks the situation SDL3
reacts to and the host's setting, not the Cocoa driver itself:

- the hint is 0 in the host;
- the game warped the hidden pointer to the window centre twice within 30 ms
  (the condition SDL3 turns into relative mode);
- the game ends by its own menu, and afterwards the host shows the Mac
  pointer and has no relative mode, no grab.

    build/analysis-venv/bin/python tools/test_host_pointer_release.py [--no-build]
"""
from __future__ import annotations

import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's
import dx9_tree  # noqa: E402
from programs_drive import COLLECTION  # noqa: E402

GAME = COLLECTION / 'GAMES/KROKO3D/krok_adventure.exe'


# Two pixels of the game's menu, in "Play" and in "Exit" (canvas
# pixels, 008400 green on black). The A5 intro before it is a white screen
# with a turning cube; neither pixel is menu green there.
MENU = ('526,160,008400', '520,345,008400')


def script(shots):
    """The hand: motion events in window points, the click as guest input.

    Nothing starts at a fixed time: the script waits until the menu is on
    the screen (wait=), and every step after it counts from that moment, so
    a loaded machine that takes longer over the intro only starts later."""
    steps, t = [], 1.0
    for pixel in MENU:
        steps.append(f'{t:.3f}:wait={pixel}')
    t += 1.0
    # Fast movement around the centre, the way a hand moves: the game
    # recentres after each packet, so its warps come a few ms apart. Several
    # bursts, so a busy machine still sees one pair of warps within 30 ms.
    for burst in range(3):
        for i in range(20):
            steps.append(f'{t:.3f}:motion={410 if i % 2 == 0 else 390},300')
            t += 0.008
        t += 0.3
    # Slowly (every packet recentred) into the top left corner, where the
    # game clamps its pointer, and from there onto "Exit" (585,345 of the
    # 800x600 canvas).
    for _ in range(20):
        steps.append(f'{t:.3f}:motion=360,260')
        t += 0.1
    for _ in range(15):
        steps.append(f'{t:.3f}:motion=439,323')
        t += 0.1
    steps.append(f'{t:.3f}:shot={shots}')
    t += 0.2
    steps.append(f'{t:.3f}:down=400,300')
    t += 0.2
    steps.append(f'{t:.3f}:up=400,300')
    return ';'.join(steps)


def finish(host, log_path, limit=600):
    """Wait until the game has ended and the host has reported the pointer
    after it, then close the window the way a person does: SIGTERM, which
    SDL turns into SDL_QUIT. No fixed quit time: under load the intro alone
    takes longer than any time chosen in advance. The limit only ends a run
    in which the game never exits."""
    deadline = time.monotonic() + limit
    while host.poll() is None and time.monotonic() < deadline:
        text = log_path.read_text(errors='replace')
        end = text.find('NEO98 HOST exited ')
        if end >= 0 and 'NEO98 HOST POINTER state ' in text[end:]:
            time.sleep(1.0)
            host.send_signal(signal.SIGTERM)
            break
        time.sleep(0.5)
    try:
        return host.wait(timeout=60 if host.poll() is None and time.monotonic() < deadline else 5)
    except subprocess.TimeoutExpired:
        host.kill()
        host.wait()
        return None


def main():
    if not GAME.is_file():
        print(f'SKIP {GAME} is not there')
        return 0
    tree = dx9_tree.require_tree()  # Kroko-Mania needs DINPUT8.DLL
    if tree is None:
        print('SKIP no DirectX 9 tree')
        return 0
    out = neo98.output_directory('host-pointer-release')
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(tree), str(drive_c)], check=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    shot = out / 'on-exit.ppm'
    shot.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_POINTER_TRACE='1',
               NEO98_HOST_INPUT=script(shot))
    env.pop('NEO98_HOST_MENU', None)
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'D:\\GAMES\\KROKO3D\\krok_adventure.exe', '--quiet',
               '--drive-c', str(drive_c), '--drive-readonly', f'D={COLLECTION}', '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        host = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL)
        returncode = finish(host, log_path)
    shutil.rmtree(drive_c, ignore_errors=True)
    text = log_path.read_text(errors='replace')
    lines = [line for line in text.splitlines() if line.startswith('NEO98 HOST')]
    centre_pairs = [float(m.group(1)) for m in
                    re.finditer(r'^NEO98 HOST POINTER warp .*centre=1 hidden=1 since-centre-ms=([0-9.]+)$', text, re.M)]
    exited = next((i for i, line in enumerate(lines) if line.startswith('NEO98 HOST exited code=0')), None)
    after = lines[exited + 1:] if exited is not None else []
    states = [line for line in lines if line.startswith('NEO98 HOST POINTER state ')]
    checks = {
        'host ended': returncode == 0,
        'SDL warp emulation off (hint 0)':
            'NEO98 HOST POINTER hint emulate-warp-with-relative=0' in lines,
        'hidden pointer warped to the centre twice within 30 ms':
            sum(1 for ms in centre_pairs if ms < 30.0) >= 1,
        'Kroko-Mania ended by "Exit"': exited is not None,
        'Mac pointer shown after the end': 'NEO98 HOST POINTER show=1' in after,
        'no relative mode, no grab at the end':
            bool(states) and ' relative=0 grab=0 ' in states[-1] + ' ' and 'shown=1' in states[-1],
        'relative mode never on': not any(' relative=1 ' in line + ' ' for line in states)
                                  and 'NEO98 HOST pointer relative mode released' not in lines,
    }
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    print(f'centre warps of the hidden pointer: {len(centre_pairs)}, '
          f'within 30 ms: {sum(1 for ms in centre_pairs if ms < 30.0)}')
    if not all(checks.values()):
        print(f'HOST POINTER RELEASE FAIL; see {log_path}')
        return 1
    print('HOST POINTER RELEASE PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
