#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Windows program Camera and the camera as a device, in the host without a window.

Every run takes the test camera of the host (NEO98_TEST_CAMERA, tools/host_devices/service.h)
instead of the Mac's camera, so no run asks macOS for camera access or opens the camera.

1. app: CAMERA.EXE (tools/host_devices/camera.c) shows the moving picture of the test camera
   through AVICAP32 and NEO98CAP.DRV: red on the left, green on the right, and two pictures a
   second apart differ. It names the driver in SYSTEM.INI [drivers] msvideo= itself.
2. states: without a picture Camera says why instead of showing black: a host without a
   camera, a refused camera access (macOS), the question for access still open, and a camera
   that sends no picture.
3. devices: the camera is a device of class MEDIA in the Device Manager, "NEO98 Camera" under
   "Sound, video and game controllers", whose sheet says it works, and the Multimedia control
   panel lists it under "Video Capture Devices"; without a camera there is no such device.
4. move: the window of Camera follows a drag of its title bar while the picture runs.
5. start: the shortcut Programs > Accessories > Multimedia > Camera the host seeds opens Camera.

    build/analysis-venv/bin/python tools/test_camera_host.py [--no-build] [--only PART]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import arm64_run  # noqa: E402
import first_logon  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from build_host_devices import build as build_hardware_manager  # noqa: E402
from test_device_manager_host import COMMAND, Script, dumps, failures, host, names_script, sheets, trees  # noqa: E402

CLASS = 'NEO98CameraViewer'
SHORTCUT = 'WINDOWS/Start Menu/Programs/Accessories/Multimedia/Camera.lnk'


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    width, height = int(parts[1]), int(parts[2])
    return width, height, parts[4][-width * height * 3:]


def seed(run):
    """The files the host seeds when its hardware manager starts, for a run of Camera alone."""
    folder = ROOT / 'build/host-devices'
    shutil.copy(folder / 'CAMERA.EXE', run / 'c/WINDOWS/CAMERA.EXE')
    shutil.copy(folder / 'NEO98CAP.DRV', run / 'c/WINDOWS/SYSTEM/Neo98CAP.DRV')


def tops(text):
    """The dumps of tops=2: [[(kind, visible, frame, class, title)]]."""
    out = []
    for line in text.splitlines():
        if line.startswith('NEO98 HOST tops '):
            out.append([])
            continue
        match = re.match(r'NEO98 HOST (top|child) id=\w+ visible=(\d) frame=(-?\d+),(-?\d+),(-?\d+),(-?\d+) .*? class=(\S+) title=(.*)$', line)
        if match and out:
            out[-1].append((match[1], match[2] == '1', tuple(int(match[i]) for i in range(3, 7)), match[7], match[8]))
    return out


def camera_windows(dump):
    """The frame of Camera's window, its visible capture window and the visible text."""
    frame, capture, words, inside = None, False, '', False
    for kind, visible, rect, klass, title in dump:
        if kind == 'top':
            inside = klass == CLASS
            if inside:
                frame = rect if visible else None
            continue
        if not inside or not visible:
            continue
        if klass.upper() == 'STATIC':
            words = words or title
        elif klass != 'STATIC':
            capture = True
    return frame, capture, words


def colours(path, frame):
    """Pure red, pure green, white and black pixels inside the frame of the window."""
    width, height, pixels = read_ppm(path)
    red = green = white = black = 0
    left, top, right, bottom = frame
    for y in range(max(top, 0), min(bottom, height)):
        row = y * width * 3
        for x in range(max(left, 0), min(right, width)):
            r, g, b = pixels[row + x * 3:row + x * 3 + 3]
            if r > 230 and g < 30 and b < 30:
                red += 1
            elif g > 230 and r < 30 and b < 30:
                green += 1
            elif r > 230 and g > 230 and b > 230:
                white += 1
            elif r < 16 and g < 16 and b < 16:
                black += 1
    return red, green, white, black


def largest_black_square(path, frame):
    """Side of the largest solid black square; text and frame lines are not an empty preview."""
    width, height, pixels = read_ppm(path)
    left, top, right, bottom = frame
    left, right = max(left, 0), min(right, width)
    previous = [0] * (right - left + 1)
    largest = 0
    for y in range(max(top, 0), min(bottom, height)):
        current = [0] * len(previous)
        for i, x in enumerate(range(left, right), 1):
            at = (y * width + x) * 3
            if max(pixels[at:at + 3]) < 16:
                current[i] = 1 + min(current[i - 1], previous[i], previous[i - 1])
                largest = max(largest, current[i])
        previous = current
    return largest


def camera_run(binary, out, name, mode, steps, deadline=40, keep=False, program='C:\\WINDOWS\\CAMERA.EXE', command='CAMERA.EXE'):
    tree = win98_c.use_pristine()
    return host(binary, out, name, program, command, steps, deadline, tree, win98_c.journal(tree), prepare=seed,
                keep=keep, environment={'NEO98_TEST_CAMERA': mode})


def app(binary, out, check, report):
    steps = [f'0:wait={CLASS}|Camera', '4:tops=2', '4.3:shot={out}/picture-1.ppm', '5.5:shot={out}/picture-2.ppm']
    code, text, run = camera_run(binary, out, 'app', 'on', steps, keep=True)
    found = tops(text)
    frame, capture, words = camera_windows(found[0]) if found else (None, False, '')
    check('app: Camera opens its window', frame is not None)
    check('app: the host started the test camera', 'NEO98 HOST camera start error=0' in text)
    check('app: the capture window shows and no text stands in its place', capture and not words)
    if frame and (run / 'picture-1.ppm').exists() and (run / 'picture-2.ppm').exists():
        red, green, white, _black = colours(run / 'picture-1.ppm', frame)
        report['app'] = {'frame': frame, 'red': red, 'green': green, 'white': white}
        check(f'app: the picture of the test camera, red left and green right ({red}, {green} pixels)',
              red > 60000 and green > 60000)
        before, after = read_ppm(run / 'picture-1.ppm')[2], read_ppm(run / 'picture-2.ppm')[2]
        check('app: the picture moves (two pictures a second apart differ)', before != after)
    else:
        check('app: the pictures were taken', False)
    ini = (run / 'c/WINDOWS/SYSTEM.INI').read_text(errors='replace') if (run / 'c/WINDOWS/SYSTEM.INI').exists() else ''
    check('app: SYSTEM.INI [drivers] names the capture driver', re.search(r'(?im)^msvideo=neo98cap\.drv\s*$', ini))
    check('app: no call the host does not carry', not failures(text))
    shutil.rmtree(run / 'c', ignore_errors=True)


STATES = [('none', 'No camera is connected to this computer.'),
          ('denied', 'NEO98 is not allowed to use the camera.'),
          ('ask', 'Waiting for permission to use the camera.'),
          ('dark', 'Starting the camera...')]


def states(binary, out, check, report):
    for mode, expected in STATES:
        steps = [f'0:wait={CLASS}|Camera', '4:tops=2', '4.3:shot={out}/state.ppm']
        code, text, run = camera_run(binary, out, 'state-' + mode, mode, steps, deadline=20)
        found = tops(text)
        frame, capture, words = camera_windows(found[0]) if found else (None, False, '')
        report.setdefault('states', {})[mode] = words
        check(f'states, {mode}: Camera says "{expected}"', words.startswith(expected) and not capture)
        if frame and (run / 'state.ppm').exists():
            red, green, _white, black = colours(run / 'state.ppm', frame)
            square = largest_black_square(run / 'state.ppm', frame)
            report.setdefault('state_pixels', {})[mode] = {'black': black, 'largest_black_square': square}
            check(f'states, {mode}: no picture and no black area (largest square {square}px)',
                  red < 100 and green < 100 and square < 32)
        else:
            check(f'states, {mode}: the screenshot was captured', False)


def devices(binary, out, check, report):
    tree = win98_c.use_pristine()
    journal = win98_c.journal(tree)
    present = {}
    for mode in ('on', 'none'):
        script = names_script()
        code, text, run = host(binary, out, 'devices-' + mode, 'C:\\WINDOWS\\RUNDLL32.EXE', COMMAND, script.steps,
                               int(script.time) + 10, tree, journal, environment={'NEO98_TEST_CAMERA': mode})
        found = trees(text)
        texts = [item[3] for item in found[-1]['items']] if found else []
        present[mode] = texts
        check(f'devices, {mode}: the tree was read', bool(texts))
        check(f'devices, {mode}: no call the host does not carry', not failures(text))
    on, off = present['on'], present['none']
    media = 'Sound, video and game controllers'
    check('devices: with a camera "NEO98 Camera" stands under Sound, video and game controllers',
          media in on and on[on.index(media) + 1:on.index(media) + 3] == ['NEO98 Camera', 'NEO98 Sound Card'])
    check('devices: without a camera there is no camera device', on and off and 'NEO98 Camera' not in off)
    report['devices'] = on
    if 'NEO98 Camera' in on:
        # The sheet of the camera: General says it works, Driver names its files.
        results = {}
        sheets(binary, out, tree, journal, [(on.index('NEO98 Camera'), 'NEO98 Camera')],
               lambda name, ok: results.__setitem__(name, ok), report, 'camera: ',
               environment={'NEO98_TEST_CAMERA': 'on'})
        for name, ok in results.items():
            if 'Resources shows' in name or 'no page Resources' in name:
                continue  # neither a device of DEVNODES nor a monitor or a drive among them
            check(name, ok)
    # Multimedia, page Devices: Video Capture Devices with the camera.
    # The tab Devices; in its tree below Multimedia Drivers the eighth class, Video Capture Devices,
    # opened with Right, its first row, and Properties. The tree there is
    # COMCTL32's own, so its rows are not dumped: the title of the sheet names
    # the row.
    steps = ['0:wait=|Multimedia Properties', '3:click=365,96', '4.5:click=120,140', '5:key=24']
    time = 5.3
    for _ in range(8):
        steps.append(f'{time:.1f}:key=28')
        time += 0.2
    steps += [f'{time:.1f}:key=27', f'{time + 0.5:.1f}:key=28', f'{time + 1:.1f}:shot={{out}}/devices.ppm',
              f'{time + 1.4:.1f}:click=357,436', f'{time + 1.6:.1f}:wait=|NEO98 Camera Properties',
              f'{time + 2.6:.1f}:tops=2', f'{time + 2.9:.1f}:shot={{out}}/camera.ppm']
    code, text, run = host(binary, out, 'multimedia', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl', steps, 40, tree, journal,
                           environment={'NEO98_TEST_CAMERA': 'on'})
    found = dumps(text)
    titles = [window['title'] for entry in found if isinstance(entry, dict) for window in entry['windows']]
    report['multimedia'] = titles
    check('devices: Multimedia lists "NEO98 Camera" under Video Capture Devices and opens its sheet',
          'NEO98 Camera Properties' in titles)
    check('devices: no call the host does not carry (Multimedia)', not failures(text))


def move(binary, out, check, report):
    # The window opens at the place USER gives CW_USEDEFAULT; the first dump
    # tells it, the drag starts on the title bar 120 pixels right of the left edge.
    steps = [f'0:wait={CLASS}|Camera', '3:tops=2']
    code, text, run = camera_run(binary, out, 'move-place', 'on', steps, deadline=15)
    found = tops(text)
    frame = camera_windows(found[0])[0] if found else None
    check('move: Camera opens its window', frame is not None)
    if not frame:
        return
    x, y = frame[0] + 120, frame[1] + 10
    results = []
    for attempt in range(3):
        dx, dy = 40 + 30 * attempt, 30 + 10 * attempt
        steps = [f'0:wait={CLASS}|Camera', '3:tops=2', f'3.5:down={x},{y}']
        time = 3.7
        for i in range(1, 9):
            steps.append(f'{time:.1f}:move={x + dx * i // 8},{y + dy * i // 8}')
            time += 0.1
        steps += [f'{time + 0.2:.1f}:up={x + dx},{y + dy}', f'{time + 1.5:.1f}:tops=2',
                  f'{time + 1.8:.1f}:shot={{out}}/moved.ppm']
        code, text, run = camera_run(binary, out, f'move-{attempt}', 'on', steps, deadline=20)
        found = tops(text)
        if len(found) < 2:
            results.append(None)
            continue
        before, after = camera_windows(found[0])[0], camera_windows(found[-1])[0]
        results.append((before, after, (dx, dy)))
        moved = before and after and (after[0] - before[0], after[1] - before[1]) == (dx, dy)
        check(f'move {attempt + 1}: a drag of the title bar by {dx},{dy} moves the window by as much', moved)
        if not moved:
            print('    before', before, 'after', after)
        if after and (run / 'moved.ppm').exists():
            red, green, _white, _black = colours(run / 'moved.ppm', after)
            check(f'move {attempt + 1}: the picture runs at the new place', red > 60000 and green > 60000)
    report['move'] = results


def start(binary, out, check, report):
    """The shortcut of the Start menu: seeded by the host into a desktop session's C:, it opens Camera."""
    drive, registry = first_logon.clone(out / 'session')
    # A fresh install must also work when Setup has no Multimedia group.
    (drive / 'WINDOWS/CAMERA.EXE').unlink(missing_ok=True)
    shutil.rmtree((drive / SHORTCUT).parent, ignore_errors=True)
    check('start: the fresh installation has no Multimedia group', not (drive / SHORTCUT).parent.exists())
    target = 'C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Multimedia\\Camera.lnk'
    steps = ['0:wait=Shell_TrayWnd', f'4:guestopen={target}', f'5:wait={CLASS}|Camera', '9:tops=2', '9.3:shot={out}/picture.ppm', '10:menu=quit']
    run = out / 'start'
    shutil.rmtree(run, ignore_errors=True)
    run.mkdir(parents=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps).replace('{out}', str(run)),
               NEO98_HOST_DEADLINE='60', NEO98_SETTINGS_DIR=str(run / 'settings'), NEO98_LOG_DIR=str(run / 'logs'),
               NEO98_TEST_CAMERA='on')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS', 'NEO98_TRACE_CALLS_FROM', 'NEO98_CPU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--registry', str(registry),
               '--crash-report-dir', str(run / 'reports')]
    result = arm64_run.run(command, cwd=run, env=env, timeout=240)
    text = result.stdout + result.stderr
    (run / 'log.txt').write_text(text)
    check('start: the host ends normally', result.returncode == 0 and not result.timed_out)
    check('start: the host put Camera.lnk into Programs > Accessories > Multimedia', (drive / SHORTCUT).exists())
    check('start: the host put CAMERA.EXE into C:\\WINDOWS', (drive / 'WINDOWS/CAMERA.EXE').exists())
    found = tops(text)
    frame, capture, _words = camera_windows(found[-1]) if found else (None, False, '')
    check('start: the shortcut opens Camera with its picture', frame is not None and capture)
    if frame and (run / 'picture.ppm').exists():
        red, green, _white, _black = colours(run / 'picture.ppm', frame)
        check('start: the shortcut shows the test camera pixels', red > 60000 and green > 60000)
    else:
        check('start: the shortcut shows the test camera pixels', False)
    check('start: no call the host does not carry', not failures(text))
    # Installing updates must preserve the user's deletion of a shortcut.
    (drive / SHORTCUT).unlink(missing_ok=True)
    env['NEO98_HOST_INPUT'] = '0:wait=Shell_TrayWnd;4:menu=quit'
    result = arm64_run.run(command, cwd=run, env=env, timeout=240)
    (run / 'restart.log').write_text(result.stdout + result.stderr)
    check('start: restart ends normally', result.returncode == 0 and not result.timed_out)
    check('start: a deleted shortcut stays deleted', not (drive / SHORTCUT).exists())
    shutil.rmtree(out / 'session', ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--only', action='append', default=[], choices=['app', 'states', 'devices', 'move', 'start'])
    arguments = parser.parse_args()
    build_hardware_manager()
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    out = neo98.output_directory('camera-host')
    out.mkdir(parents=True, exist_ok=True)
    checks, report = {}, {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    for name, part in (('app', app), ('states', states), ('devices', devices), ('move', move), ('start', start)):
        if not arguments.only or name in arguments.only:
            part(binary, out, check, report)
    (out / 'results.json').write_text(json.dumps({'checks': checks, 'report': report}, indent=1, default=str) + '\n')
    failed = [name for name, ok in checks.items() if not ok]
    print(f'{len(checks) - len(failed)}/{len(checks)} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
