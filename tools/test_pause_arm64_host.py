#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The pause of the machine for an ARM64 program.

An ARM64 program runs on a virtual processor of Hypervisor.framework and can
read the counter of that processor (CNTVCT_EL0) without any call of the
system. tools/pause_arm64_fixture.c reads it beside GetTickCount,
QueryPerformanceCounter and the wall clock on a timer of 50 ms. The host
pauses the machine for four seconds. No counter may make a step of half a
second, the wall clock makes the step of the pause.

A host with a virtual processor is never killed: after the
time limit it is asked to end and waited for. The test does not start while
another NEO98 host runs.

    build/analysis-venv/bin/python tools/test_pause_arm64_host.py [--no-build]
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
import arm64_run  # noqa: E402
import arm64_sdk  # noqa: E402

PAUSE = 4.0


def main():
    out = neo98.output_directory('pause-arm64-host')
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    others = subprocess.run(['pgrep', '-f', 'neo98-host|Neo98.app/Contents/MacOS'], capture_output=True, text=True).stdout.split()
    if others:
        print(f'PAUSE ARM64 SKIP: another NEO98 host is alive (pid {", ".join(others)}); two hosts with a '
              'virtual processor at once are not started')
        return 77
    signature = subprocess.run(['codesign', '-d', '--entitlements', '-', str(binary)], capture_output=True, text=True)
    if 'com.apple.security.hypervisor' not in signature.stdout + signature.stderr:
        print('PAUSE ARM64 NOT RUN: the host binary has no hypervisor entitlement')
        return 2
    pristine = win98_c.use_pristine()
    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', '-c', '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    arm64_sdk.build([ROOT / 'tools/pause_arm64_fixture.c'], drive_c / 'APAUSE.EXE', gui=True)
    steps = ['0:wait=|APause ready', '1:pause=1', f'{1 + PAUSE}:pause=0', '5.2:wait=|APause done', '5.5:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_INPUT=';'.join(steps),
               NEO98_HOST_DEADLINE='60')
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree',
               '--exe', 'C:\\APAUSE.EXE', '--quiet', '--drive-c', str(drive_c), '--no-registry-file',
               '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'
    with log_path.open('w') as log, arm64_run.lock():
        arm64_run.sync()
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL)
        try:
            code = process.wait(120)
        except subprocess.TimeoutExpired:
            arm64_run.end(process)
            code = 'timeout'
    text = log_path.read_text(errors='replace')
    answer = drive_c / 'APAUSE.TXT'
    values = {}
    if answer.exists():
        for line in answer.read_text().splitlines():
            key, _, value = line.partition('=')
            values[key] = int(value)
    shutil.rmtree(drive_c, ignore_errors=True)
    off = re.search(r'^NEO98 HOST pause off \(script\) stood=([\d.]+) s', text, re.M)
    low, high = (PAUSE - 0.5) * 1000, (PAUSE + 0.7) * 1000
    checks = {
        'host ended': code == 0,
        'the ARM64 program finished': bool(values),
        'the machine stood about four seconds': bool(off) and abs(float(off.group(1)) - PAUSE) < 0.5,
    }
    if values:
        checks.update({
            'GetTickCount made no step of half a second': values['tick_step'] < 500,
            'QueryPerformanceCounter made no step of half a second': values['qpc_step'] < 500,
            'the counter of the processor (CNTVCT_EL0) made no step of half a second': values['counter_step'] < 500,
            'the wall clock made the step of the pause': low < values['wall_step'] < high + 300,
            'the wall clock ran the pause longer than the tick count':
                low < values['wall_elapsed'] - values['tick_elapsed'] < high,
            'the wall clock ran the pause longer than the counter of the processor':
                low < values['wall_elapsed'] - values['counter_elapsed'] < high,
            'the timer did not fire for the pause': values['timers'] <= values['tick_elapsed'] / 50 + 2,
        })
    (out / 'results.json').write_text(json.dumps({'log': str(log_path), 'exit_code': code, 'values': values,
                                                  'checks': checks}, indent=2) + '\n')
    for key, value in values.items():
        print(f'     {key} = {value}')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'PAUSE ARM64 FAIL; see {log_path}')
        return 1
    print('PAUSE ARM64 PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
