#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""TELNET.EXE of the imported tree opens its window and shows no message box.

Telnet without a host name asks WS2_32 for Winsock and, if that fails, puts up
a message box whose text it has not loaded yet, so the box stood there with a
caption and an empty line. The cause was WSAStartup of
the original WS2_32 failed over a pseudo handle. What this run pins is the
visible half. Telnet has to reach its own window `Telnet - (None)`, and no
visible `#32770` may stand anywhere on the desktop.

    build/analysis-venv/bin/python tools/test_telnet_native.py
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from native_app import ROOT, build_fast
import win98_c  # noqa: E402
win98_c.use_pristine()  # tests start from setup's C:, never the user's

WINDOW = re.compile(r'^PROBE_WINDOW (.*)$', re.M)


def fields(line):
    out = {}
    for part in line.split(' '):
        key, _, value = part.partition('=')
        if key in ('class', 'text'):
            try:
                value = bytes.fromhex(value).decode('cp1252', 'replace')
            except ValueError:
                value = ''
        out[key] = value
    return out


def main():
    tree = win98_c.FOLDER
    program = tree / 'WINDOWS/TELNET.EXE'
    if not program.is_file():
        raise SystemExit(f'{program} is missing; run python3 neo98.py setup --iso ...')
    out = ROOT / 'build/telnet-native'
    out.mkdir(parents=True, exist_ok=True)
    runner = build_fast('probe_application.cpp', 'probe-smoke')
    command = [str(runner), str(program), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', str(tree),
               '--drive-c', str(out / 'drive-c'),
               '--time-limit', '30',
               '--screenshot', str(out / 'telnet.ppm')]
    result = subprocess.run(command, capture_output=True, text=True, timeout=300)
    log = result.stdout + result.stderr
    (out / 'telnet.log').write_text(log)
    windows = [fields(line) for line in WINDOW.findall(result.stdout)]
    started = re.search(r'^PROBE started=1 state=(\d+) code=\d+ calls=(\d+)', result.stdout, re.M)
    main_window = next((w for w in windows
                        if w['class'] == 'TelnetWClass' and w['visible'] == '1'), None)
    box = next((w for w in windows if w['class'] == '#32770' and w['visible'] == '1'), None)
    checks = []

    def check(name, ok, detail):
        checks.append({'check': name, 'ok': bool(ok), 'detail': detail})

    check('telnet run alive', bool(started) and started.group(1) != '3',
          started.group(0) if started else 'no PROBE line')
    check('telnet window', main_window is not None and main_window['text'] == 'Telnet - (None)',
          main_window['text'] if main_window else 'none')
    check('no message box', box is None, box['text'] if box else 'none')
    check('no empty MessageBox text', 'MessageBox caption=Telnet text=' not in log,
          'no MessageBox line' if 'MessageBox caption=Telnet text=' not in log
          else 'Telnet still puts up its empty box')
    (out / 'results.json').write_text(json.dumps(
        {'command': command, 'checks': checks,
         'calls': int(started.group(2)) if started else 0}, indent=1) + '\n')
    for entry in checks:
        print(f"TELNET {'ok  ' if entry['ok'] else 'FAIL'} {entry['check']}: {entry['detail']}")
    if any(not entry['ok'] for entry in checks):
        raise SystemExit('TELNET FAILED')
    print(f"TELNET PASS, {started.group(2)} API calls")


if __name__ == '__main__':
    main()
