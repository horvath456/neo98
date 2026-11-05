#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""SETUPX rebuilds the driver index whole after a new INF.

Windows 98 keeps the [Manufacturer] lines of every INF in
C:\\WINDOWS\\INF\\DRVIDX.BIN and DRVDATA.BIN and rebuilds both when the INF
folder changed (SETUPX DiBuildDriverIndex, and ). LocalFree of the
16-bit KERNEL had been lost (Win16LocalOperation, src/ne_loader.cpp), so
SETUPX's local heap filled up after the first INF, every further INF failed to
open and the index kept the entries of one file: after Plus! 98 put its INFs
there the Add Printer wizard of the user's machine had no printers to offer.

The test puts one more INF into a clone of setup's C:, opens the Add Printer
wizard, which rebuilds the index, goes to the page of manufacturers and
compares the rebuilt files with setup's. Without a window.

    build/analysis-venv/bin/python tools/test_driver_index_host.py [--no-build] [--binary=HOST]
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402
from test_wheel_host import Steps  # noqa: E402

# An INF of no device class: it changes the folder, not the index.
EXTRA_INF = ('[Version]\r\nSignature="$CHICAGO$"\r\nClass=Unknown\r\nProvider=NEO98\r\n\r\n'
             '[Strings]\r\nNote="NEO98 driver index test"\r\n')


def main():
    pristine = win98_c.use_pristine()
    journal = win98_c.journal(pristine)
    out = neo98.output_directory('driver-index-host')
    out.mkdir(parents=True, exist_ok=True)
    given = [a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--binary=')]
    binary = Path(given[0]) if given else ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    setup_inf = Path(pristine) / 'WINDOWS' / 'INF'
    before = {name: (setup_inf / name).stat().st_size for name in ('DRVIDX.BIN', 'DRVDATA.BIN')}

    def prepare(run):
        (run / 'c' / 'WINDOWS' / 'INF' / 'NEO98TST.INF').write_bytes(EXTRA_INF.encode('ascii'))

    # Next on the welcome page reaches the manufacturers, which need the index.
    steps = Steps('#32770|Add Printer Wizard', 3.0)
    steps.add('click=360,352', 2.0).add('tops=1')
    code, text, run = host(binary, out, 'rebuild', 'C:\\WINDOWS\\RUNDLL32.EXE',
                           'RUNDLL32.EXE SHELL32,PrintersGetCommand_RunDLL 0,18,0,WinUtils_NewObject',
                           steps.steps, int(steps.time) + 240, pristine, journal, prepare=prepare, keep=True)
    inf = run / 'c' / 'WINDOWS' / 'INF'
    after = {name: (inf / name).stat().st_size for name in before}
    data = (inf / 'DRVDATA.BIN').read_bytes()
    infs = set(re.findall(rb'[A-Z0-9_]+\.INF', data))
    checks = {
        'the Add Printer wizard opens': 'wait=#32770|Add Printer Wizard reached' in text,
        'the rebuilt index is as large as setup\'s': after == before,
        'the index names the devices of many INFs': len(infs) >= 100,
        'no crash or call the host does not carry': code == 0 and not failures(text) and 'CRASHREPORT' not in text,
    }
    for name, ok in checks.items():
        print(('ok  ' if ok else 'FAIL'), name)
    print(f'{sum(checks.values())}/{len(checks)} checks pass; setup {before}, rebuilt {after}, '
          f'{len(infs)} INFs in the index; run {run}')
    import shutil
    shutil.rmtree(run / 'c', ignore_errors=True)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
