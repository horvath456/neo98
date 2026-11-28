#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A disabled combo box has a disabled edit field.

EnableWindow on a combo box sends it WM_ENABLE, and USER's combo box hands
the state on to its edit field and its list. The field of a disabled box is
disabled too: it takes no input, and when it paints, its parent is asked
for the colours of a static control (WM_CTLCOLORSTATIC, Windows 95 and
later), so it shows grey where a field that takes input is white. The port
left the field enabled: the Name box of the Sounds applet, disabled while
a group of events is selected, looked like a field to type into and took
no tap.

tools/combo_enable_fixture.c, measured with the same program in Windows 95
OSR2 and Windows 98 SE: a drop-down combo box, enabled, disabled and
enabled again, each time with the state of its field and the colour
messages its painting sends.

    build/analysis-venv/bin/python tools/test_combo_enable_host.py [--no-build]
"""
from __future__ import annotations
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    if not _shutil.which('i686-w64-mingw32-gcc'):  # the fixture is built with it; docs/BUILDING.md
        print('SKIP: needs i686-w64-mingw32-gcc')
        _sys.exit(77)

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

# What Windows 95 and 98 answer; of the colour messages only whether they
# come (Windows sends two or three for one paint).
EXPECTED = {
    'edit': '1',
    'enabled.edit': '1', 'enabled.ctlcoloredit': 'some', 'enabled.ctlcolorstatic': '0',
    'disabled.combo': '0', 'disabled.edit': '0', 'disabled.ctlcoloredit': '0', 'disabled.ctlcolorstatic': 'some',
    'again.edit': '1', 'again.ctlcoloredit': 'some', 'again.ctlcolorstatic': '0',
}


def matches(value, expected):
    return value is not None and (value.isdigit() and int(value) > 0 if expected == 'some' else value == expected)


def main():
    pristine = win98_c.use_pristine()
    out = neo98.output_directory('combo-enable')
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive = out / 'c'
    subprocess.run(['cp', '-a', '--reflink=auto', f'{pristine}/.', str(drive)], check=True)
    subprocess.run(['i686-w64-mingw32-gcc', '-Os', str(ROOT / 'tools/combo_enable_fixture.c'), '-ffreestanding',
                    '-fno-builtin', '-nostartfiles', '-nostdlib', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-lgdi32', '-o', str(drive / 'COMBOEN.EXE')], check=True)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='60',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    with (out / 'log.txt').open('wb') as log:
        subprocess.run([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'C:\\COMBOEN.EXE', '--quiet',
                        '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(out / 'c.registry'),
                        '--crash-report-dir', str(out / 'reports')], cwd=out, env=env, stdout=log,
                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=180)
    written = win98_c.path('C:\\COMBOEN.TXT', drive)
    lines = written.read_text(errors='replace').splitlines() if written else []
    got = dict(line.split('=', 1) for line in lines if '=' in line)
    shutil.rmtree(drive, ignore_errors=True)
    checks = {'the fixture ran to its end': got.get('done') == '1'}
    for name, expected in EXPECTED.items():
        checks[f'{name}: {got.get(name)} (Windows: {expected})'] = matches(got.get(name), expected)
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        print('    log:', out / 'log.txt')
    print(f'combo enable host: {sum(map(bool, checks.values()))} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
