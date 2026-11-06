#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A restart of Windows while an ARM64 program runs.

RESTART.EXE (ARM64, tools/arm64_restart_fixture.c) is the program of the host
and calls ExitWindowsEx(EWX_REBOOT). The host takes the machine of the ARM64
process down and re-executes itself; the program starts again, finds its mark
and stays. The test then ends the host with SIGTERM and waits.

This is the one case in which a host process holding a virtual processor
replaces itself. Run it alone and deliberately; it is not part of any suite.

    build/analysis-venv/bin/python tools/test_arm64_restart_host.py [--no-build]
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import neo98  # noqa: E402
import win98_c  # noqa: E402
from native_app import ROOT, build_host  # noqa: E402
import arm64_run  # noqa: E402
import arm64_sdk  # noqa: E402


def main():
    out = neo98.output_directory('arm64-restart-host')
    for name in ('drive-c', 'reports', 'settings'):
        shutil.rmtree(out / name, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    pristine = win98_c.use_pristine()
    drive_c = out / 'drive-c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(pristine) + '/', str(drive_c)], check=True)
    (drive_c / win98_c.PRISTINE_MANIFEST).unlink(missing_ok=True)
    arm64_sdk.build([ROOT / 'tools/arm64_restart_fixture.c'], drive_c / 'RESTART.EXE', gui=False)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_SETTINGS_DIR=str(out / 'settings'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_KEYMAP', 'NEO98_HOST_INPUT'):
        env.pop(key, None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'), '--system-tree', '--exe',
               'C:\\RESTART.EXE', '--drive-c', str(drive_c), '--no-registry-file', '--crash-report-dir', str(out / 'reports')]
    log_path = out / 'host.log'

    def marks():
        path = next((p for p in drive_c.iterdir() if p.name.upper() == 'RESTART.TXT'), None)
        return path.read_text(errors='replace').split() if path else []
    arm64_run.ensure_idle()
    arm64_run.sync()
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + 120
        while process.poll() is None and time.monotonic() < deadline and 'second=1' not in marks():
            time.sleep(0.5)
        time.sleep(2)
        alive = process.poll() is None
        code = arm64_run.end(process)
    seen = marks()
    text = log_path.read_text(errors='replace')
    checks = {
        'the program ran and asked for the restart': 'first=1' in seen,
        'the host restarted Windows': 'NEO98 HOST reboot' in text or 'NEO98 HOST exitwindows' in text,
        'the program runs again after the restart': 'second=1' in seen,
        'the host is alive after the restart and ends on SIGTERM': alive and code == 0,
    }
    shutil.rmtree(drive_c, ignore_errors=True)
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    if not all(checks.values()):
        print(f'ARM64 RESTART FAIL: marks={seen} code={code}; see {log_path}')
        return 1
    print(f'ARM64 RESTART PASS checks={len(checks)} marks={seen}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
