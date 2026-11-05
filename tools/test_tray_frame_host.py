#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sunken edge around the tray icons and the clock is complete.

TrayNotifyWnd has WS_EX_STATICEDGE: one shadow line above and left, one
highlight line below and right. The clock is a child that reaches one point
past the client area of TrayNotifyWnd, over its lower and right edge. USER
took the whole rectangle of a child out of the parent's visible region, so
the parent never drew its edge there, and under the clock a line in the
colour of the desktop stayed where the highlight belongs. A child only
stands in the client area of its parent.

Explorer runs as the shell without a screen on a desktop of 800x600 and of
1470x827 (seamless, with the taskbar shown: the seamless view hides it by
default). The edge is looked up from the lower right corner of
the taskbar: the highlight column, the highlight row below the icons and the
clock in one piece, the shadow row above.

    build/analysis-venv/bin/python tools/test_tray_frame_host.py [--no-build]
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

SHADOW, LIGHT = b'\x80\x80\x80', b'\xff\xff\xff'


def main():
    out = neo98.output_directory('tray-frame-host')
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    checks = {}
    for name, extra, screen in (('800x600', [], None), ('1470x827', ['--seamless', '--seamless-taskbar'], '1470x827')):
        drive_c, shot = out / f'drive-c-{name}', out / f'{name}.ppm'
        shutil.rmtree(drive_c, ignore_errors=True)
        shot.unlink(missing_ok=True)
        subprocess.run(['cp', '-c', '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
        steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', f'3:shot={shot}', '3.2:menu=quit']
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='240')
        if screen:
            env['NEO98_HOST_SCREEN'] = screen
        env.pop('NEO98_HOST_ATTACHED', None)
        env.pop('NEO98_HOST_MENU', None)
        command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                   '--system-tree', '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--quiet', '--drive-c', str(drive_c),
                   '--no-registry-file', '--crash-report-dir', str(out / 'reports'), *extra]
        with (out / f'{name}.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL, timeout=300)
        shutil.rmtree(drive_c, ignore_errors=True)
        checks[f'{name}: host ended'] = result.returncode == 0
        checks[f'{name}: screenshot written'] = shot.exists()
        if not shot.exists():
            continue
        magic, size, depth, body = shot.read_bytes().split(b'\n', 3)
        width, height = (int(v) for v in size.split())

        def at(x, y):
            return body[(y * width + x) * 3:(y * width + x) * 3 + 3]
        # The taskbar ends at the lower edge of the desktop; TrayNotifyWnd
        # stands two points from its right and lower edge, 22 points high.
        right, bottom = width - 3, height - 3
        top = bottom - 21
        left = right
        while left > 0 and at(left - 1, top) == SHADOW:
            left -= 1
        checks[f'{name}: desktop of that size'] = (width, height) == tuple(int(v) for v in name.split('x'))
        checks[f'{name}: shadow row above icons and clock ({right - left} points)'] = right - left >= 60
        checks[f'{name}: shadow column on the left'] = all(at(left, y) == SHADOW for y in range(top, bottom))
        checks[f'{name}: highlight column on the right'] = all(at(right, y) == LIGHT for y in range(top, bottom + 1))
        missing = [x for x in range(left, right + 1) if at(x, bottom) != LIGHT]
        checks[f'{name}: highlight row below icons and clock in one piece ({len(missing)} points missing)'] = not missing
    (out / 'results.json').write_text(json.dumps({'checks': checks}, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'TRAY FRAME FAIL; see {out}')
        return 1
    print('TRAY FRAME PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
