#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Wayland compositor of neo98-kms (tools/host_wayland/compositor.cpp)
with the test client tools/test_wayland_client.c in place of Kodi.

neo98-kms runs with the picture in memory (NEO98_KMS_OUTPUT=image:1280x720),
without DISPLAY and without sound, and shows the client's toplevel over the
whole screen (NEO98_TEST_COMPOSITOR=fullscreen).

  dmabuf the client finds wl_compositor, xdg_wm_, zwp_linux_dmabuf_v1
          version 4 with feedback (the render node, linear XRGB8888),
          wl_output, wl_seat and wl_shm; it is configured to 1280x720 and
          its linear dmabuf from GBM fills the screen in red, then in green
          after "colour"; when it ends, Windows is back
  shm the same with a wl_shm buffer in blue, and frame callbacks at
          least 30 times a second

    build/analysis-venv/bin/python tools/test_compositor_host.py [--no-build] [--only PART...]


"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import neo98  # noqa: E402
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

PARTS = ('dmabuf', 'shm')
CLIENT = ROOT / 'build/native-app/test-wayland-client'


def build_client():
    include, _, objects = native_app.wayland_protocols()
    flags = subprocess.check_output(['pkg-config', '--cflags', 'libdrm'], text=True).split()
    subprocess.run(['clang', '-O2', '-Wall', '-Wextra', '-Wno-unused-parameter', '-I', str(include), *flags,
                    str(ROOT / 'tools/test_wayland_client.c'), *map(str, objects), '-o', str(CLIENT),
                    '-lwayland-client', '-lgbm'], env=neo98.tool_environment(), check=True)


def pixel(picture, x, y):
    width, _, data = picture
    i = (y * width + x) * 3
    return data[i] << 16 | data[i + 1] << 8 | data[i + 2]


def run(out, name, client_args, colours):
    """neo98-kms with Notepad and the client: (host log, client lines, pictures)."""
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    steps = ['0:wait=Notepad', '4:shot={out}/first.png', '6:shot={out}/second.png', '9:shot={out}/after.png', '10:quit=1']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KMS_OUTPUT='image:1280x720', NEO98_TEST_COMPOSITOR='fullscreen',
               NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(folder)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_TEST_KODI'):
        env.pop(variable, None)
    log = (folder / 'host.txt').open('w')
    host = subprocess.Popen([str(ROOT / 'build/native-app/neo98-kms'), '--ram-drive', '--no-registry-file', '--exe',
                             r'C:\WINDOWS\NOTEPAD.EXE', '--time-limit', '40'], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    socket = runtime = None
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and not socket:
        match = re.search(r'compositor socket=(\S+) dir=(\S+)', (folder / 'host.txt').read_text())
        if match:
            socket, runtime = match[1], match[2]
        time.sleep(0.05)
    lines = []
    if socket:
        client_env = dict(os.environ, WAYLAND_DISPLAY=socket, XDG_RUNTIME_DIR=runtime)
        client = subprocess.Popen([str(CLIENT), '--colour', colours[0], *client_args], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, env=client_env)
        def after(step):
            end = time.monotonic() + 40
            while time.monotonic() < end and f'{step}.png' not in (folder / 'host.txt').read_text():
                time.sleep(0.05)
        # The colour changes once the first picture is taken, the client
        # ends after the second.
        after('first')
        client.stdin.write(f'colour {colours[1]}\n')
        client.stdin.flush()
        after('second')
        client.stdin.write('quit\n')
        client.stdin.flush()
        lines = client.communicate(timeout=20)[0].splitlines()
        (folder / 'client.txt').write_text('\n'.join(lines) + '\n')
    host.wait(timeout=60)
    log.close()
    pictures = {n: read_png(folder / f'{n}.png') for n in ('first', 'second', 'after') if (folder / f'{n}.png').exists()}
    return (folder / 'host.txt').read_text(), lines, pictures, host.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', nargs='+', choices=PARTS)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    build_client()
    pristine = win98_c.use_pristine()
    out = ROOT / 'build/compositor-host'
    out.mkdir(parents=True, exist_ok=True)
    results, failed = {}, []

    def check(name, condition):
        results[name] = bool(condition)
        print(('PASS ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    for part in args.only or PARTS:
        shm = part == 'shm'
        colours = ('1030d0', 'e0c010') if shm else ('d01010', '10c010')
        text, lines, pictures, code = run(out, part, ['--shm'] if shm else [], colours)
        seen = '\n'.join(lines)
        check(f'{part}: the compositor listens', 'NEO98 HOST compositor socket=' in text)
        check(f'{part}: the client finds every global it needs',
              'globals compositor=1 xdg=1 dmabuf=4 output=1 seat=1 shm=1' in seat_line(lines))
        if not shm:
            check(f'{part}: dmabuf feedback names a render node with linear XRGB8888',
                  re.search(r'feedback device=226:\d+ linear-xrgb=1', seen))
        check(f'{part}: the output is 1280x720 and the toplevel is configured to it', 'output 1280x720' in lines and 'configure 1280 720' in lines)
        check(f'{part}: the buffer is a {"wl_shm buffer" if shm else "linear dmabuf"}', f'buffer {"shm" if shm else "dmabuf"} 1280x720' in lines)
        first, second = int(colours[0], 16), int(colours[1], 16)
        ok = 'first' in pictures and all(pixel(pictures['first'], x, y) == first for x, y in ((0, 0), (640, 360), (1279, 719)))
        check(f'{part}: the client\'s colour fills the screen', ok)
        check(f'{part}: the new colour shows', 'second' in pictures and pixel(pictures['second'], 640, 360) == second)
        check(f'{part}: when the client ends, Windows is back',
              'after' in pictures and pixel(pictures['after'], 5, 5) == 0 and pixel(pictures['after'], 640, 360) not in (first, second))
        frames = max([int(l.split('=')[1]) for l in lines if l.startswith('end frames=')] or [0])
        check(f'{part}: frame callbacks keep coming ({frames} in at least 6 s)', frames >= (180 if shm else 30))
        check(f'{part}: the host ends cleanly', code == 0 and 'compositor client left' in text)
    assert not win98_c.pristine_problems(pristine), 'The test changed the pristine C:'
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    if failed:
        raise SystemExit(f'COMPOSITOR FAIL: {len(failed)} of {len(results)}, see {out}')
    print(f'COMPOSITOR PASS: {len(results)} checks')


def seat_line(lines):
    return next((l for l in lines if l.startswith('globals ')), '')


if __name__ == '__main__':
    main()
