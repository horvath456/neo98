#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Fonts folder of the Control Panel and the font viewer behind it.

LZ32.DLL lies in the shared arena above 2 GB, so its DllMain connects its
thunk to SYSTHUNK.DLL once for the session; SYSTRAY loads it first. FONTEXT.DLL
in Explorer then reached the shared jump to the LZ gate without a connection of
Explorer's own and the session ended with "flat thunk to SYSTHUNK.DLL##0
not supported" (src/x86_flat_thunk.cpp). The test opens C:\\WINDOWS\\FONTS on the
desktop of the first logon and double-clicks Arial, which starts FONTVIEW.

    build/analysis-venv/bin/python tools/test_fonts_folder_host.py [--no-build]
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import first_logon  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402


def main():
    out = neo98.output_directory('fonts-folder-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    drive, registry = first_logon.clone(out / 'session')
    # Arial is the first icon of the folder's window at 64,55.
    steps = ['0:wait=Shell_TrayWnd', '1:guestopen=C:\\WINDOWS\\FONTS', '2:wait=CabinetWClass|Fonts', '10:tops=1',
             '12:dblclick=107,195', '13:wait=#32770|Arial (TrueType)', '20:tops=1']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='50', NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    env.pop('NEO98_HOST_ATTACHED', None)
    with (out / 'host.log').open('w') as log:
        code = subprocess.run([str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
                               '--exe', 'C:\\WINDOWS\\EXPLORER.EXE', '--drive-c', str(drive), '--registry', str(registry),
                               '--crash-report-dir', str(out / 'reports')], cwd=ROOT, env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=240).returncode
    text = (out / 'host.log').read_text(errors='replace')
    checks = {
        'the Fonts folder opens': 'wait=CabinetWClass|Fonts reached' in text,
        'Explorer reads the fonts through LZ32 without a connection of its own':
            'not supported' not in text and 'class=CabinetWClass title=Fonts' in text,
        'a double click on Arial opens the font viewer': 'wait=#32770|Arial (TrueType) reached' in text,
        'no crash or failure': code == 0 and 'NEO98 HOST failed' not in text and 'CRASHREPORT' not in text,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass, log {out / "host.log"}')
    subprocess.run(['rm', '-rf', str(out / 'session')])
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
