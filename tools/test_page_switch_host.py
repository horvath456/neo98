#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A property sheet changes pages without a control of the old page left over.

Internet Properties, General to Connections: the click gives the tab the focus,
the Address edit of the General page redraws without its selection, COMCTL32
puts the old page to the bottom (SetWindowPos HWND_BOTTOM) and the tab erases
its body until the new page is made. USER's EDIT redraws itself at the change
of focus, before the erase; the port's EDIT redrew at the next frame, after it,
and stood alone on the empty tab until the Connections page appeared
(src/user_visible.inc, SettleGeometry). The test takes a picture every 30 ms
after the click: no picture may show the edit without the rest of the page.
Over a clone of setup's C:, without a window.

    build/analysis-venv/bin/python tools/test_page_switch_host.py [--no-build]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402
from test_wheel_host import Steps, picture  # noqa: E402


def uniform(run, name, box):
    """Whether every pixel of `box` in the screenshot has one colour."""
    data = (run / f'{name}.ppm').read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    left, top, right, bottom = box
    pixels = {parts[3][(y * width + x) * 3:(y * width + x) * 3 + 3] for y in range(top, bottom) for x in range(left, right)}
    return len(pixels) == 1


FRAMES = 20
# The sheet at 64,55: the Address edit of the General page and its Delete
# Files button below it.
EDIT = (203, 159, 431, 177)
BUTTON = (266, 282, 346, 305)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    arguments = parser.parse_args()
    pristine = win98_c.use_pristine()
    journal = win98_c.journal(pristine)
    out = neo98.output_directory('page-switch-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    steps = Steps('#32770|Internet Properties', 3.0)
    steps.add('shot={out}/general.ppm', 0.5).add('click=258,95', 0.0)
    for i in range(FRAMES):
        steps.add(f'shot={{out}}/f{i:02d}.ppm', 0.03)
    steps.add('shot={out}/connections.ppm', 3.0, before=3.0)
    code, text, run = host(binary, out, 'switch', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE SHELL32.DLL,Control_RunDLL INETCPL.CPL', steps.steps,
                           int(steps.time) + 20, pristine, journal)
    names = ['general'] + [f'f{i:02d}' for i in range(FRAMES)] + ['connections']
    if not all((run / f'{name}.ppm').is_file() for name in names):
        check('all pictures taken', False)
        return 1
    edit = {name: picture(run, name, EDIT) for name in names}
    button = {name: picture(run, name, BUTTON) for name in names}
    # Between the pages, with neither the General page's button nor the
    # Connections page there, the tab's body is empty where the edit was.
    between = [name for name in names[1:-1]
               if button[name] not in (button['general'], button['connections'])]
    alone = [name for name in between if not uniform(run, name, EDIT)]
    check('the Connections page replaces the General page', edit['connections'] != edit['general'])
    check('no picture shows the Address edit without its page', not alone)
    check('no crash or call the host does not carry', code == 0 and not failures(text) and 'CRASHREPORT' not in text)
    print(f'{sum(checks.values())}/{len(checks)} checks pass; {len(between)} pictures between the pages, '
          f'edit alone in {alone}; pictures in {run}')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
