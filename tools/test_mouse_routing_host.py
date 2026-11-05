#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Mouse routing on retrieval and by hit test, in the host with Explorer as the shell.

Three hand checks in one session:

- Drag the grip between Quick Launch and the task buttons with the button
  held. The rebar takes the capture on the press and resizes its band
  on the moves; the task button moves right.
- Start, then a click on "Shut Down..." whose press and release reach the host
  together. The press goes to the menu band that holds the capture;
  the band hands it to the toolbar, which peeks for its own WM_LBUTTONDOWN
  and then takes the capture. USER routes the release only when a retrieval
  takes it, so it reaches the toolbar and the Shut Down dialog opens. Before,
  the filtered peek fixed the release on the band and nothing happened.
- A double click on the tray clock. TrayClockWClass and TrayNotifyWnd
  answer WM_NCHITTEST with HTTRANSPARENT, Shell_TrayWnd with HTCAPTION, so
  the point search of USER hands WM_NCLBUTTONDBLCLK to Shell_TrayWnd, and
  Explorer starts `rundll32 shell32.dll,Control_RunDLL timedate.cpl`
  (EXPLORER.EXE 4016DC, 40976C). Before, the router picked the clock by
  geometry and the double click was lost.

    build/analysis-venv/bin/python tools/test_mouse_routing_host.py [--no-build]

Results in build/mouse-routing-host/results.json.
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


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def at(image, x, y):
    body, width = image
    i = (y * width + x) * 3
    return body[i:i + 3].hex()


def black(image, left, right, top=582, bottom=592):
    return sum(at(image, x, y) == '000000' for x in range(left, right) for y in range(top, bottom))


def main():
    out = neo98.output_directory('mouse-routing-host')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    shots = {name: out / f'{name}.ppm' for name in ('before', 'band', 'dialog', 'clock')}
    for path in shots.values():
        path.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # Explorer needs up to a minute on the first start of a fresh C:, and a
    # busy machine a few seconds more before the tray takes a drag. The
    # grip of the task band stands at x 141, "Shut Down..." at 95,557, the
    # clock at 757,587, Cancel of the Shut Down dialog at 423,310.
    # The script waits for the guest, not for the clock (NEO98_HOST_INPUT of
    # tools/neo98_host.cpp), so a loaded machine only makes it slower.
    steps = ['0:wait=Shell_TrayWnd', '0:wait=|Welcome to Windows 98', '0:idle=2000',
             f'1:shot={shots["before"]}',
             '2:move=141,587', '2.5:down=141,587', '3:move=160,587', '3.5:move=200,587',
             '4:move=250,587', '4.5:up=250,587', '4.5:idle=1000', f'6:shot={shots["band"]}',
             '7:click=25,587', '7:idle=500', '10:move=95,557', '10:idle=500', '10.5:click=95,557',
             '10.5:wait=#32770|Shut Down Windows', '10.5:idle=1000', f'14:shot={shots["dialog"]}',
             '15:move=423,310', '15.5:click=423,310', '15.5:idle=1000',
             '18:dblclick=757,587', '18:wait=#32770|Date/Time Properties', '18:idle=2000',
             f'20:shot={shots["clock"]}', '21:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    text = log_path.read_text(errors='replace')
    present = all(path.exists() for path in shots.values())
    check('screenshots written', present)
    if present:
        before, band = pixels(shots['before']), pixels(shots['band'])
        # The caption "Welcome to Windows 98" of the task button: before the
        # drag it starts at x 170, after it right of the grip at 250.
        check('task button text before the drag', black(before, 165, 245) > 0)
        check('band drag moves the task button',
              black(band, 165, 245) == 0 and black(band, 280, 400) > 0)
        dialog = pixels(shots['dialog'])
        check('fast click opens Shut Down',
              at(dialog, 300, 166) == '000080' and at(dialog, 245, 166) == '000080')
        clock = pixels(shots['clock'])
        check('clock double click shows Date/Time',
              at(clock, 300, 67) == '000080' and at(clock, 400, 67) == '000080')
    check('Explorer starts timedate.cpl', 'Control_RunDLL timedate.cpl' in text)
    check('host ended', result.returncode == 0)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'MOUSE ROUTING FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'MOUSE ROUTING PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
