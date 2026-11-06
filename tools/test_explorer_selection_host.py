#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The selected item of an Explorer folder keeps its highlight when the window is activated again.

My Computer opens in the web view; a click selects Printers, and the label
shows white on navy because the list view has the focus. Two ways used to
take that focus away for good:

1. A click on the caption of the active folder window. USER activates a
   window on a press outside the client area
   and leaves the focus where it is; the port also set it to the frame,
   SHDOCVW passed it to SHELLDLL_DefView, and the ATL control in the page
   (SHDOC401, still UI-active) never gave it back to its list view.
2. A click on the desktop and back on the caption. The desktop runs on
   another thread of Explorer. Under Win98 every thread has its own queue,
   which keeps its focus window while another thread's window is active; the
   port has one queue, so it now notes the focus of a thread whose window
   loses the activation to another thread and hands it back on return
   (src/x86_activation.cpp).

The host runs under SDL's dummy video driver with NEO98_HOST_INPUT, which
waits for the guest instead of the clock. Checks: the navy highlight behind
the label "Printers" after the selection, after the caption click and after
the round trip over the desktop.

    build/analysis-venv/bin/python tools/test_explorer_selection_host.py [--no-build]

Results in build/explorer-selection-host/results.json.
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


def navy_behind_label(path):
    """Navy pixels in the label box of "Printers" (x 305..361, y 208..223)."""
    data = Path(path).read_bytes()
    magic, size, depth, body = data.split(b'\n', 3)
    width = int(size.split()[0])
    return sum(body[(y * width + x) * 3:(y * width + x) * 3 + 3] == b'\x00\x00\x80'
               for y in range(208, 224) for x in range(305, 362))


def main():
    out = neo98.output_directory('explorer-selection-host')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    Path(str(drive_c) + '.registry').unlink(missing_ok=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'session.registry'
    shutil.copyfile(win98_c.journal(win98_c.FOLDER), registry)
    shots = {name: out / f'{name}.ppm' for name in ('selected', 'caption', 'desktop', 'back')}
    for path in shots.values():
        path.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # My Computer on the desktop at 37,20; the folder window opens with its
    # caption at y 67 and Printers at 333,190; 700,350 is empty desktop.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=2000', '1:dblclick=37,20', '1:wait=CabinetWClass',
             '1:idle=3000', '2:click=333,190', '2:idle=1500', f'3:shot={shots["selected"]}',
             '4:click=300,67', '4:idle=1500', f'5:shot={shots["caption"]}',
             '6:click=700,350', '6:idle=1500', f'7:shot={shots["desktop"]}',
             '8:click=300,67', '8:idle=1500', f'9:shot={shots["back"]}', '10:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    present = all(path.exists() for path in shots.values())
    check('screenshots written', present)
    counts = {name: navy_behind_label(path) for name, path in shots.items() if path.exists()}
    if present:
        check('Printers selected with the highlight', counts['selected'] > 200)
        check('highlight stays after a click on the caption', counts['caption'] > 200)
        check('highlight back after the desktop and the caption', counts['back'] > 200)
    check('host ended', result.returncode == 0)
    shutil.rmtree(drive_c, ignore_errors=True)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps,
              'navy_pixels': counts, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'EXPLORER SELECTION FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'EXPLORER SELECTION PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
