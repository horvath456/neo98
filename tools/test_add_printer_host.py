#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Add Printer wizard of the Printers folder, up to its copy of files.

Explorer starts "RUNDLL32.EXE SHELL32,PrintersGetCommand_RunDLL...", which
loads MSPRINT2.DLL. Its DllMain connects two flat thunks to the 16-bit
MSPRINT.DLL and refused to load when they failed, so the wizard never showed
(src/x86_flat_thunk.cpp). On its pages: the list of printers sets tab stops
(LB_SETTABSTOPS, src/x86_listbox.cpp), and Finish runs SETUPX, which uses CLI
around its INT 12h vector and calls small real-mode routines of its own
through it (src/ne_loader.cpp, Win16SetupxRealModeCall). The test clicks Next
through the pages and Finish, and ends at the prompt for the Windows 98 CD,
because the driver files lie in its cabinets. Over a clone of setup's C:,
without a window.

A second run removes the driver database (DRVIDX.BIN and DRVDATA.BIN in INF), so
Next first shows the "Building driver information database" dialog of SETUPX,
which then builds without reading a message: SetWindowPos erases a window of
its thread when it shows it, and the dialog has its background at once
(src/x86_dialog.cpp, src/x86_position.cpp). Then it picks Apple among the
manufacturers, whose dialog code in MSPRINT maps points with USER.258 and
animates with USER.448 (src/win16_user.cpp); without them the wizard ended.

    build/analysis-venv/bin/python tools/test_add_printer_host.py [--no-build]
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
from test_wheel_host import Steps  # noqa: E402
from test_support import active_caption_pair  # noqa: E402

# Next and Finish of the wizard at 64,55 on the 800 by 600 canvas.
NEXT = 'click=360,352'
# The dialog of SETUPX at 269,230: its caption at 500,241, and 515,300 in it
# right of the wizard, where the desktop lies under it. Apple in the list of
# manufacturers at 98,203, its row selected at 180,203 right of the pointer.
CAPTION, INSIDE, APPLE, ROW = (500, 241), (515, 300), (98, 203), (180, 203)
CAPTION_LEFT = (480, 241)
NAVY, FACE = (0, 0, 128), (192, 192, 192)
FRAMES = 20


def pixel(path, point):
    data = path.read_bytes()
    parts = data.split(b'\n', 3)
    width = int(parts[1].split()[0])
    x, y = point
    return tuple(parts[3][(y * width + x) * 3:(y * width + x) * 3 + 3])


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n', 1)[0])
    parser.add_argument('--no-build', action='store_true')
    arguments = parser.parse_args()
    pristine = win98_c.use_pristine()
    journal = win98_c.journal(pristine)
    out = neo98.output_directory('add-printer-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if arguments.no_build else build_host()
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    # Welcome, manufacturer and model, port, name, test page, Finish.
    steps = Steps('#32770|Add Printer Wizard', 3.0)
    steps.add('tops=1', 0.5)
    # SETUPX may rebuild its driver database at the first Next (setup's C:
    # can have INF files newer than its index); the second run gives the
    # build 30 s as well.
    for page in range(5):
        steps.add(NEXT, 1.5, before=30.0 if page == 1 else 0.0)
    steps.add('wait=#32770|Copying Files...', 4.0).add('tops=4')
    code, text, run = host(binary, out, 'wizard', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE SHELL32,PrintersGetCommand_RunDLL 0,18,0,WinUtils_NewObject',
                           steps.steps, int(steps.time) + 30, pristine, journal)
    check('MSPRINT2.DLL connects its thunks to the hosted MSPRINT.DLL',
          'FLATTHUNK connect hosted dll16=MSPRINT.DLL name=MspThk_ThunkData16' in text
          and 'FLATTHUNK connect hosted dll16=MSPRINT.DLL name=MspFThk_ThunkData16' in text
          and 'DLL init refused module=C:\\WINDOWS\\SYSTEM\\MSPRINT2.DLL' not in text)
    check('the wizard opens', 'wait=#32770|Add Printer Wizard reached' in text)
    check('the list of printers takes its tab stops', 'Unsupported listbox message' not in text)
    check('Finish copies files and asks for the Windows 98 CD',
          'wait=#32770|Copying Files... reached' in text
          and ('class=#32770 title=Insert Disk' in text or text.count('class=#32770 title=Copying Files...') >= 2))
    check('no crash or call the host does not carry',
          code == 0 and not failures(text) and 'CRASHREPORT' not in text)

    def without_database(run):
        for name in ('DRVDATA.BIN', 'DRVIDX.BIN'):
            (run / 'c/WINDOWS/INF' / name).unlink(missing_ok=True)

    steps = Steps('#32770|Add Printer Wizard', 3.0)
    steps.add(NEXT, 0.1)
    for i in range(FRAMES):
        steps.add(f'shot={{out}}/f{i:02d}.ppm', 0.1)
    steps.add('click=%d,%d' % APPLE, 4.0, before=30.0).add('shot={out}/apple.ppm', 0.5)
    code, text, run = host(binary, out, 'database', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE SHELL32,PrintersGetCommand_RunDLL 0,18,0,WinUtils_NewObject',
                           steps.steps, int(steps.time) + 30, pristine, journal, prepare=without_database)
    shots = [run / f'f{i:02d}.ppm' for i in range(FRAMES)]
    have = all(p.is_file() for p in shots) and (run / 'apple.ppm').is_file()
    shown = [p for p in shots if have and active_caption_pair(
        bytes(pixel(p, CAPTION_LEFT)).hex(), bytes(pixel(p, CAPTION)).hex())]
    bare = [p.stem for p in shown if pixel(p, INSIDE) != FACE]
    check('the database dialog has its background from its first picture on', have and shown and not bare)
    check('Apple lists its printers and the wizard stays', have and pixel(run / 'apple.ppm', ROW) == NAVY)
    check('no crash or call the host does not carry while it builds and picks',
          code == 0 and not failures(text) and 'CRASHREPORT' not in text)
    print(f'{sum(checks.values())}/{len(checks)} checks pass, log {run / "log.txt"}')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
