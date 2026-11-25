#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/network_setup.py reads the INF files of the folder it imports from.

The registry import (tools/import_registry.py) reads the hives of the Win98
folder tools/win98_c.py names (NEO98_WIN98_ROOT, a test's pristine copy).
network_setup.py adds the network card and the client to them from
NETAMD.INF and NETCLI.INF, and read those from build/win98-c, which a tree
with its drive elsewhere does not have. This test gives it a folder of its
own whose NETCLI.INF names the provider differently and checks that the
client's values come from there.

    python3 tools/test_network_setup_inf.py
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import win98_c  # noqa: E402

MARK = 'Inf Folder Probe'
PROBE = '''
import sys
sys.path[:0] = [{tools!r}]
import network_setup
values, table = network_setup.install('NETCLI.INF', 'VREDIR.ndi', 'probe')
print('PROVIDER=' + table['v_ms'])
'''


def main():
    win98_c.require()
    source = win98_c.path('C:\\WINDOWS\\INF\\NETCLI.INF')
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary) / 'c'
        (folder / 'WINDOWS/INF').mkdir(parents=True)
        text = source.read_bytes().decode('cp1252')
        lines = [f'V_MS="{MARK}"' if line.replace(' ', '').lower().startswith('v_ms=') else line
                 for line in text.split('\r\n')]
        (folder / 'WINDOWS/INF/NETCLI.INF').write_bytes('\r\n'.join(lines).encode('cp1252'))
        env = dict(os.environ, NEO98_WIN98_ROOT=str(folder))
        for variable in ('NEO98_WIN98_LIVE_ROOT', 'NEO98_WIN98_PRISTINE'):
            env.pop(variable, None)
        run = subprocess.run([sys.executable, '-c', PROBE.format(tools=str(ROOT / 'tools'))], env=env,
                             capture_output=True, text=True)
    found = [line[len('PROVIDER='):] for line in run.stdout.splitlines() if line.startswith('PROVIDER=')]
    checks = {
        'the provider of this folder\'s NETCLI.INF replaced its V_MS': any(MARK in line for line in lines),
        f'network_setup reads NETCLI.INF of NEO98_WIN98_ROOT: {found or run.stderr.strip()[-200:]}':
            found == [MARK],
    }
    for name, passed in checks.items():
        print('ok  ' if passed else 'FAIL', name, flush=True)
    if not all(checks.values()):
        return 1
    print(f'network setup inf: all {len(checks)} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
