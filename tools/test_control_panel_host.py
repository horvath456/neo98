#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Start > Settings > Control Panel in the host with Explorer as the shell.

Explorer opens the folder through ShellExecuteEx, whose `ddeexec` sends
`[ViewFolder("", :<shared pidl>:<pid>, 1)]` over raw DDE to the "Folders"
service of SHDOC401. SHDOC401's DDEML callback keeps a list of
its conversations, which it fills on XTYP_CONNECT_CONFIRM and
searches on XTYP_EXECUTE. Before the fix the port sent
the notification with the class XCLASS_DATA (2072h) instead of
XCLASS_NOTIFICATION (8072h), the list stayed empty and every command was
refused with 0; the confirm handler then also needs DdeQueryConvInfo with the
Win32 CONVINFO (60h bytes, hwndPartner at 5Ch) for a conversation whose client
has no DDEML.

The host runs under SDL's dummy video driver with NEO98_HOST_INPUT (Start,
Settings, Control Panel, screenshot) and NEO98_HOST_MENU (quit). Checks:

- the DDE execute is answered with DDE_FACK (8000h),
- a CabinetWClass window is created,
- the screenshot shows a caption over the desktop where the folder opens,
- the 16-bit applets (Add New Hardware, Modems, Network) show their own
  icons.

    build/analysis-venv/bin/python tools/test_control_panel_host.py [--no-build]

Results in build/control-panel-host/results.json.
"""
from __future__ import annotations

import json
import os
import re
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


def main():
    out = neo98.output_directory('control-panel-host')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    shot = out / 'folder.ppm'
    shot.unlink(missing_ok=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    # Explorer needs up to a minute on the first start of a fresh C:, more
    # on a loaded machine, so the script waits for the taskbar and a quiet
    # guest instead of a fixed time (tools/neo98_host.cpp, NEO98_HOST_INPUT).
    # The Start menu item "Settings" is at y 421, its submenu opens to the
    # right with "Control Panel" first.
    steps = ['0:wait=Shell_TrayWnd', '0:idle=2000', '1:click=25,587', '1:idle=500',
             '2:move=95,421', '2.5:move=96,421', '2.5:idle=500', '5:move=200,421',
             '6:move=230,423', '6:idle=500', '7:down=230,423', '7.3:up=230,423',
             '8:wait=CabinetWClass', '8:idle=2000', f'9:shot={shot}', '10:menu=quit']
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
    answers = re.findall(r'DDEML frame execute command=\[ViewFolder\(.*?answer=([0-9a-f]+)', text)
    check('ViewFolder sent over DDE', answers)
    check('ViewFolder answered with DDE_FACK', answers and all(int(a, 16) & 0x8000 for a in answers))
    check('folder window created', 'CreateWindow class=CabinetWClass' in text)
    # the network client of tools/network_setup.py names no provider
    # whose DLL is missing; MPRSERV would say so in a box at the start.
    check('no "Windows Networking" box at the start', 'MessageBox caption=Windows Networking' not in text)
    check('screenshot written', shot.exists())
    if shot.exists():
        image = pixels(shot)
        # The folder window opens at the top left of the work area over the
        # desktop icons; its active caption is navy from x 70 on.
        caption = [y for y in range(40, 120) if at(image, 300, y) == '000080' and at(image, 400, y) == '000080']
        check('folder caption on the screen', caption)
        # the 16-bit applets show their own icons, not the generic
        # one of ten colours. SHELL32 names their file through Shl3216 index
        # 0Dh with the instance LoadLibrary16 answered  and takes
        # the icon from the NE resources; the icons sit on the large-icon
        # grid of the folder: Add New Hardware, Modems, Network.
        body, width = image
        def colours(cx, cy):
            return len({body[(y * width + x) * 3:(y * width + x) * 3 + 3]
                        for y in range(cy - 16, cy + 16) for x in range(cx - 16, cx + 16)})
        applets = {'Add New Hardware': (332, 188), 'Modems': (332, 338), 'Network': (257, 413)}
        counts = {name: colours(*place) for name, place in applets.items()}
        checks['16-bit applet icons ' + json.dumps(counts)] = all(n > 20 for n in counts.values())
    check('host ended', result.returncode == 0)
    report = {'log': str(log_path), 'exit_code': result.returncode, 'steps': steps,
              'answers': answers, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'CONTROL PANEL FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'CONTROL PANEL PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
