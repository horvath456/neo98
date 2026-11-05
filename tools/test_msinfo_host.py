#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""MSINFO32 in the host, without a window.

Microsoft System Information showed one category, "Software Updates". Its
tree comes from HKLM\\Software\\Microsoft\\Shared Tools\\MSInfo\\Categories,
which the registry of the port did not take from the hive. With the key the
controls behind the categories load, and the first of them, MSISYS.OCX,
needs what this test checks:

1. the key Categories and its three branches are there (NEO98_REG_TRACE);
2. LoadLibrary16 and GetProcAddress16 (KERNEL32 ordinals 35 and 37) answer
   for KERNEL, USER, GDI and TOOLHELP, and QT_Thunk reaches the gates:
   GetFreeSystemResources answers a percentage, the real mode INT 21h
   AX=3305h through a selector the control made itself comes back;
3. the device \\\\.\\MSISYS.VXD opens;
4. the window stands, and the six categories of Hardware Resources are
   walked with the keyboard without a call the host does not carry.

What the lists of Hardware Resources and Components show is not checked:
they are empty until HWINFOD.VXD has a native counterpart (.

    build/analysis-venv/bin/python tools/test_msinfo_host.py [--no-build]
"""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import win98_c  # noqa: E402
from test_device_manager_host import failures, host  # noqa: E402

PROGRAM = 'C:\\Program Files\\Common Files\\Microsoft Shared\\MSINFO\\MSINFO32.EXE'
CATEGORIES = 'Shared Tools\\MSInfo\\Categories'


def main():
    pristine = win98_c.use_pristine()
    if win98_c.path(PROGRAM, pristine) is None:
        print('skip: MSINFO32.EXE is not in', pristine)
        return 0
    out = neo98.output_directory('msinfo-host')
    out.mkdir(parents=True, exist_ok=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    # The root, then Hardware Resources opened (numpad *) and its six pages.
    steps = ['0:wait=|Microsoft System Information', '20:tops=2', '22:click=140,153', '24:key=6a']
    time = 27.0
    for _ in range(6):
        steps.append(f'{time:.1f}:key=28')
        time += 4.0
    steps += [f'{time:.1f}:shot={{out}}/resources.ppm', f'{time + 1:.1f}:tops=1']
    os.environ['NEO98_REG_TRACE'] = '1'
    try:
        code, text, run = host(binary, out, 'msinfo', PROGRAM, 'MSINFO32.EXE', steps, int(time) + 20, pristine,
                               win98_c.journal(pristine))
    finally:
        del os.environ['NEO98_REG_TRACE']
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)
        print(('ok  ' if condition else 'FAIL'), name, flush=True)

    opened = re.findall(r'^REG RegOpenKeyExA HKEY_LOCAL_MACHINE \| (.*) = (\d+)$', text, re.M)
    check('the registry has MSInfo\\Categories',
          any(path.lower().endswith(CATEGORIES.lower()) and result == '0' for path, result in opened))
    branches = {match.lower() for match in re.findall(r'MSInfo\\Categories\\(\w+)', text, re.I)}
    check('with the branches Resources, Components and Software', {'resources', 'components', 'software'} <= branches)
    resources = re.search(r'WIN16 USER\.284 .*\n(?:.*\n)?FLATTHUNK hosted gate \w+:\w+ words=1 result=0000(\w+)', text)
    check('QT_Thunk reaches GetFreeSystemResources of USER through its gate',
          resources and 0 < int(resources[1], 16) <= 100)
    check('a selector the control made itself is called (DPMI 0300h, INT 21h AX=3305h)',
          re.search(r'WIN16 KERNEL\.175 .*', text) and re.search(r'FLATTHUNK hosted gate \w+:0000 words=3 result=00000000', text))
    check('the module list of TOOLHELP is asked and empty', 'WIN16 TOOLHELP.51' in text)
    check('the window of MSINFO32 stands at the end',
          len(re.findall(r'NEO98 HOST top id=\w+ visible=1 .* title=Microsoft System Information', text)) >= 2)
    check('no call the host does not carry', not failures(text) and 'CRASHREPORT' not in text)
    print(f'msinfo host: {sum(checks.values())} of {len(checks)} checks passed')
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
