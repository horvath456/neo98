#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Start > Shut Down... in the host with Explorer as the shell.

SHELL32's ExitWindowsDialog creates a full-screen WS_POPUP with
WS_EX_TOPMOST|WS_EX_TOOLWINDOW, dims it with PatBlt DPa through a
halftone brush and runs the dialog 0x428 with that window as the
owner. The dialog inherits WS_EX_TOPMOST from its owner and is
DS_CENTER, so USER centres it in the work area. Before the fix the
dialog manager wrote the template's extended style a second time after the
creation, the dialog lost WS_EX_TOPMOST and lay under the dimmed window; the
user saw only the dimming.

The host runs under SDL's dummy video driver with NEO98_HOST_INPUT (Start,
Shut Down..., screenshot, Cancel, screenshot) and NEO98_HOST_MENU (quit). The
checks read the screenshots:

- the caption of the dialog stands in the middle of the work area,
- desktop and taskbar carry the halftone of the dimming, and no remnant of
  the Start menu shows under it,
- Cancel takes the dimmed window away.

The placement core in src/win98_indirect_dialog.h is checked on its own with a small native program.

    build/analysis-venv/bin/python tools/test_shutdown_dialog_host.py [--no-build]

Results in build/shutdown-dialog/results.json.
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
from test_support import write_plain_desktop_registry  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

PLACEMENT = r'''
#include "win98_indirect_dialog.h"
#include <stdio.h>
using namespace Win98IndirectDialog;
static int failures = 0;
static void expect(const char *name, int left, int top, int want_left, int want_top) {
    const bool ok = left == want_left && top == want_top;
    if (!ok) ++failures;
    printf("%s %s: %d,%d (want %d,%d)\n", ok ? "ok  " : "FAIL", name, left, top, want_left, want_top);
}
int main() {
    Placement p;
    p.monitor = p.screen = {0, 0, 800, 600};
    p.work = {0, 0, 800, 572};
    p.win40 = true;
    int l = 0, t = 0;
    p.style = 0x90c808c0u; p.owner = true;                       // Shut Down: DS_CENTER
    Place(p, 0, 0, 323, 173, l, t); expect("DS_CENTER in the work area", l, t, 238, 199);
    p.win40 = false;
    Place(p, 10, 20, 323, 173, l, t); expect("DS_CENTER ignored before 4.0", l, t, 10, 20);
    p.win40 = true; p.style = 0x90c800c0u; p.owner_x = 100; p.owner_y = 50;
    Place(p, 10, 20, 200, 100, l, t); expect("relative to the owner's client", l, t, 110, 70);
    p.style |= 1;
    Place(p, 10, 20, 200, 100, l, t); expect("DS_ABSALIGN on the screen", l, t, 10, 20);
    p.style &= ~1u; p.owner = false;
    Place(p, 10, 20, 200, 100, l, t); expect("no owner on the screen", l, t, 10, 20);
    p.owner = true;
    Place(p, 650, 500, 200, 100, l, t); expect("kept inside the work area", l, t, 600, 468);
    p.extended = 0x80;
    Place(p, 650, 500, 200, 100, l, t); expect("a tool window keeps to the monitor", l, t, 600, 496);
    p.extended = 0; p.style = 0x50000000u | 0x40;                // WS_CHILD, owner is the parent
    Place(p, 10, 20, 200, 100, l, t); expect("a child stays inside its parent", l, t, 10, 20);
    p.style = 0x90c818c0u; p.cursor_x = 400; p.cursor_y = 300;   // DS_CENTERMOUSE
    Place(p, 0, 0, 200, 100, l, t); expect("DS_CENTERMOUSE around the cursor", l, t, 300, 250);
    return failures ? 1 : 0;
}
'''


def pixels(path):
    """The host writes binary PPM (P6): magic, size, depth, then RGB rows."""
    magic, size, depth, body = Path(path).read_bytes().split(b'\n', 3)
    return body, int(size.split()[0])


def at(image, x, y):
    body, width = image
    i = (y * width + x) * 3
    return body[i:i + 3].hex()


def halftone(image, x, y):
    """The dimming ANDs a checkerboard: two neighbours, one black, one not."""
    a, b = at(image, x, y), at(image, x + 1, y)
    return (a == '000000') != (b == '000000')


def main():
    out = neo98.output_directory('shutdown-dialog')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    source = out / 'placement.cpp'
    source.write_text(PLACEMENT)
    program = out / 'placement'
    subprocess.run(['clang++', '-std=c++17', '-I', str(ROOT / 'src'), str(source), '-o', str(program)], check=True)
    placement = subprocess.run([str(program)], capture_output=True, text=True)
    print(placement.stdout, end='')
    check('placement core ', placement.returncode == 0)

    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    registry = out / 'plain-desktop.registry'
    write_plain_desktop_registry(registry)
    for name in ('menu.ppm', 'dialog.ppm', 'cancel.ppm'):
        (out / name).unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # Explorer needs up to a minute on the first start of a fresh C:.
    # Press and release lie apart, the menu takes the capture on the press.
    # The script waits for the guest, not for the clock (NEO98_HOST_INPUT of
    # tools/neo98_host.cpp), so a loaded machine only makes it slower.
    # On setup's C: the Welcome window of the first logon has periodic
    # timers and the machine is never quiet for long; a short idle interval
    # as in tools/test_mouse_routing_host.py.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=200', '1:click=25,587', '1:idle=1000',
             f'2:shot={out / "menu.ppm"}', '2.5:move=95,557', '3:down=95,557',
             '3.3:up=95,557', '3.3:wait=#32770|Shut Down Windows', '3.3:idle=1000',
             f'5:shot={out / "dialog.ppm"}', '6:move=423,310', '7:down=423,310',
             '7.3:up=423,310', '7.3:idle=1500', f'8:shot={out / "cancel.ppm"}', '9:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps))
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet',
               '--drive-c', str(drive_c), '--registry', str(registry),
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    # The first start of a fresh C: takes one to two minutes before the
    # desktop is quiet for a second: the whole run measured 60 to 225 s, and
    # 300 s were sometimes too few.
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=600)
    shots = all((out / name).exists() for name in ('menu.ppm', 'dialog.ppm', 'cancel.ppm'))
    check('screenshots written', shots)
    if shots:
        # The Start menu: the item "Shut Down..." on its gray face.
        check('Start menu open', at(pixels(out / 'menu.ppm'), 150, 557) == 'c0c0c0')
        dialog, cancel = pixels(out / 'dialog.ppm'), pixels(out / 'cancel.ppm')
        # USER centres the template size (323x252 with all its buttons) in
        # 800x572, at 238,160; WM_INITDIALOG then shrinks the window to the
        # buttons it keeps and leaves the top where it is. The caption row
        # lies at y 163..180; left of x 238 is the dimmed desktop.
        # Win98 SE draws the active caption as a gradient from #000080 to
        # #1084D0; its left end is still #000080.
        def blue(value):
            r, b = int(value[:2], 16), int(value[4:], 16)
            return r <= 0x10 and b >= 0x80 and b - r >= 0x70
        check('caption of the dialog in the middle of the work area',
              blue(at(dialog, 300, 166)) and blue(at(dialog, 245, 166))
              and not blue(at(dialog, 236, 166)))
        check('dialog face is not dimmed', at(dialog, 250, 320) == 'c0c0c0' and at(dialog, 251, 320) == 'c0c0c0')
        check('desktop dimmed', halftone(dialog, 600, 100) and halftone(dialog, 100, 540))
        check('taskbar dimmed', halftone(dialog, 400, 590))
        check('no Start menu under the dimming', at(dialog, 100, 300) in ('008080', '000000')
              and at(dialog, 101, 300) in ('008080', '000000'))
        check('Cancel takes the dimming away', not halftone(cancel, 600, 100) and not halftone(cancel, 400, 590)
              and at(cancel, 300, 208) != '000080')
    check('host ended', result.returncode == 0)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'SHUTDOWN DIALOG FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'SHUTDOWN DIALOG PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
