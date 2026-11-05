#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Where COM1 to COM4 lead is a setting of the Mac app.

One run of build/native-app/neo98-host without a command line, under SDL's
dummy video driver:

1. settings.ini of NEO98_SETTINGS_DIR names `com.2 = tcp:127.0.0.1:PORT`; the
   host hands that endpoint to the comm provider at its start and the menu
   Machine > Serial ports shows it.
2. The menu gives COM1 a serial device and takes the cable off COM2; both are
   in settings.ini afterwards.
3. The restart of the machine comes up with the endpoints of the file.

That a port with an endpoint really carries its bytes there is
tools/test_comm_host_native.py; this test is about the setting.

The machine folder is a clone of the pristine Win98 folder, the settings and
logs live under build/com-settings; nothing outside the checkout is touched.

    build/analysis-venv/bin/python tools/test_com_settings_host.py [--no-build]

docs/BUILDING.md.
"""
from __future__ import annotations

import json
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
from test_mac_app_host import clone_machine, run_host  # noqa: E402

ENDPOINT = 'tcp:127.0.0.1:7898'
DEVICE = '/dev/cu.neo98-test'


def main():
    out = neo98.output_directory('com-settings')
    for name in ('settings', 'logs', 'tmp', 'machine'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    machine = clone_machine(out / 'machine')
    settings_dir, logs = out / 'settings', out / 'logs'
    settings_dir.mkdir()
    (out / 'tmp').mkdir()
    settings = settings_dir / 'settings.ini'
    settings.write_text(f'# Test\nmachine = {machine}\nscale = 1\nseamless = 0\ncom.2 = {ENDPOINT}\n')
    environment = {'HOME': str(Path.home()), 'SDL_VIDEODRIVER': 'dummy', 'SDL_AUDIODRIVER': 'dummy',
                   'NEO98_SETTINGS_DIR': str(settings_dir), 'NEO98_LOG_DIR': str(logs), 'TMPDIR': str(out / 'tmp'),
                   'NEO98_HOST_INPUT_ONCE': '1', 'NEO98_HOST_DEADLINE': '600'}
    steps = ['0:wait=Shell_TrayWnd', '1:idle=1500', '1:menu=describe',
             f'1:menu=com-1={DEVICE}', '1:menu=com-2=', '1:menu=com-5=tcp:127.0.0.1:1', '1:menu=describe',
             '1:menu=reboot', '0:wait=Shell_TrayWnd', '1:idle=1500', '1:menu=describe', '1:menu=quit']
    code, log = run_host([str(binary)], dict(environment, NEO98_HOST_INPUT=';'.join(steps)), out / 'host.log')
    checks = []

    def check(name, condition, detail=''):
        checks.append({'check': name, 'ok': bool(condition), 'detail': str(detail)})

    starts = [m.start() for m in re.finditer(r'^NEO98 HOST app settings=', log, re.M)]
    check('two starts, the second after the restart', len(starts) == 2 and code == 0, f'starts={len(starts)} exit={code}')
    first = log[starts[0]:starts[1]] if len(starts) == 2 else log
    second = log[starts[1]:] if len(starts) == 2 else ''
    menus = re.split(r'NEO98 HOST menu-script describe ok', log)
    lines = lambda text: re.findall(r'^NEO98 HOST menu-item .*COM\d: .*$', text, re.M)
    check('the start takes com.2 from settings.ini', f'NEO98 HOST COM2 -> {ENDPOINT}' in first.split('menu-script')[0],
          re.findall(r'^NEO98 HOST COM\d -> .*$', first, re.M)[:2])
    menu_start = lines(menus[0]) if menus else []
    check('the menu lists four ports, COM2 with its endpoint',
          len(menu_start) == 4 and any(f'COM2: {ENDPOINT}' in line for line in menu_start) and
          sum('disconnected' in line for line in menu_start) == 3, menu_start)
    check('the menu gives COM1 a device', f'NEO98 HOST COM1 -> {DEVICE}' in first and
          f'menu-script com-1={DEVICE} ok' in first)
    check('the menu takes the cable off COM2', 'NEO98 HOST COM2 -> (disconnected)' in first)
    check('there is no fifth port', 'menu-script com-5=tcp:127.0.0.1:1 refused' in first)
    menu_changed = lines(menus[1]) if len(menus) > 1 else []
    check('the menu shows the change', any(f'COM1: {DEVICE}' in line for line in menu_changed) and
          sum('disconnected' in line for line in menu_changed) == 3, menu_changed)
    text = settings.read_text()
    check('settings.ini keeps COM1 and drops COM2', f'com.1 = {DEVICE}' in text and 'com.2' not in text,
          [line for line in text.splitlines() if line.startswith('com.')])
    check('the restart comes up with the file', f'NEO98 HOST COM1 -> {DEVICE}' in second.split('menu-script')[0] and
          'NEO98 HOST COM2' not in second.split('menu-script')[0],
          re.findall(r'^NEO98 HOST COM\d -> .*$', second, re.M)[:2])
    ok = all(c['ok'] for c in checks)
    (out / 'results.json').write_text(json.dumps({'checks': checks}, indent=1) + '\n')
    for c in checks:
        print(('ok  ' if c['ok'] else 'FAIL') + f" {c['check']}" + (f": {c['detail']}" if not c['ok'] else ''))
    print(f"COM SETTINGS {'PASS' if ok else 'FAIL'} {sum(c['ok'] for c in checks)}/{len(checks)}")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
