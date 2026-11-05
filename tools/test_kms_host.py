#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""neo98-kms, the Linux host without X11 or GTK.

The runs show the picture in memory (NEO98_KMS_OUTPUT=image:1280x720)
instead of on the screen, without DISPLAY or WAYLAND_DISPLAY and without
sound. The real screen needs a free console; the part busy only checks the
message a running desktop gets.

  libraries the program needs neither GTK, GDK nor an X11 or Wayland
             client library of its own
  picture Notepad starts; its 800x600 desktop stands in the middle of the
             1280x720 picture with black bars left and right
  keys typed text reaches Notepad (the edit field changes)
  busy with a desktop on the screen, the real output says so and how to
             start NEO98 from a text console

    build/analysis-venv/bin/python tools/test_kms_host.py [--no-build] [--only PART...]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402
from test_media_center_host import read_png  # noqa: E402

PARTS = ('libraries', 'picture', 'keys', 'busy')
BINARY = ROOT / 'build/native-app/neo98-kms'


def run(out, name, steps, output='image:1280x720', program=r'C:\WINDOWS\NOTEPAD.EXE', environment=None, seconds=40):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KEYMAP='US', NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(folder)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_OUTPUT', 'NEO98_KMS_CARD', 'NEO98_TEST_KODI'):
        env.pop(variable, None)
    if output:
        env['NEO98_KMS_OUTPUT'] = output
    env.update(environment or {})
    result = subprocess.run([str(BINARY), '--ram-drive', '--no-registry-file', '--exe', program, '--time-limit', str(seconds)],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=seconds + 60)
    (folder / 'log.txt').write_text(result.stdout + result.stderr)
    return result.returncode, result.stdout + result.stderr, folder


def pixel(picture, x, y):
    width, _, data = picture
    i = (y * width + x) * 3
    return data[i] << 16 | data[i + 1] << 8 | data[i + 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', nargs='+', choices=PARTS)
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    pristine = win98_c.use_pristine()
    out = ROOT / 'build/kms-host'
    out.mkdir(parents=True, exist_ok=True)
    results, failed = {}, []

    def check(name, condition):
        results[name] = bool(condition)
        print(('PASS ' if condition else 'FAIL ') + name, flush=True)
        if not condition:
            failed.append(name)

    for part in args.only or PARTS:
        if part == 'libraries':
            needed = subprocess.check_output(['readelf', '-d', str(BINARY)], text=True)
            names = [line.split('[')[1].rstrip(']') for line in needed.splitlines() if '(NEEDED)' in line]
            check('libraries: no GTK, GDK, X11 or Wayland client library of its own',
                  not [n for n in names if any(k in n for k in ('gtk', 'gdk-3', 'X11', 'xcb', 'wayland-client'))])
            check('libraries: DRM and libinput', 'libdrm.so.2' in names and 'libinput.so.10' in names)
        elif part == 'picture':
            code, text, folder = run(out, part, ['0:wait=Notepad', '2:shot={out}/notepad.png', '3:quit=1'])
            check('picture: the image output opens', 'NEO98 KMS output image 1280x720' in text)
            check('picture: Notepad starts', 'NEO98 KMS input wait=Notepad' in text)
            if (folder / 'notepad.png').exists():
                picture = read_png(folder / 'notepad.png')
                check('picture: 1280x720', picture[:2] == (1280, 720))
                check('picture: black bars of 160 pixels left and right',
                      all(pixel(picture, x, y) == 0 for x in (0, 159, 1120, 1279) for y in (0, 360, 719)))
                check('picture: the teal desktop between them', pixel(picture, 170, 700) == 0x008080 and pixel(picture, 1110, 10) == 0x008080)
            else:
                check('picture: the picture was taken', False)
            check('picture: the host ends cleanly', code == 0)
        elif part == 'keys':
            code, text, folder = run(out, part, ['0:wait=Notepad', '2:shot={out}/before.png', '3:type=Hallo KMS', '4:shot={out}/after.png', '5:quit=1'])
            if (folder / 'before.png').exists() and (folder / 'after.png').exists():
                before, after = read_png(folder / 'before.png'), read_png(folder / 'after.png')
                changed = sum(1 for x in range(245, 400) for y in range(125, 145) if pixel(before, x, y) != pixel(after, x, y))
                check(f'keys: the typed text shows in the edit field ({changed} pixels)', changed > 100)
            else:
                check('keys: the pictures were taken', False)
        elif part == 'busy':
            if not (os.environ.get('WAYLAND_DISPLAY') or os.environ.get('DISPLAY')):
                print('SKIP busy: no desktop runs; the real screen is not taken over by a test')
                continue
            code, text, folder = run(out, part, ['5:quit=1'], output=None, seconds=10)
            check('busy: the screen of a running desktop is not taken',
                  code == 1 and 'Another program shows the screen' in text and 'Ctrl+Alt+F3' in text)
    assert not win98_c.pristine_problems(pristine), 'The test changed the pristine C:'
    (out / 'results.json').write_text(json.dumps(results, indent=1) + '\n')
    if failed:
        raise SystemExit(f'KMS HOST FAIL: {len(failed)} of {len(results)}, see {out}')
    print(f'KMS HOST PASS: {len(results)} checks')


if __name__ == '__main__':
    main()
