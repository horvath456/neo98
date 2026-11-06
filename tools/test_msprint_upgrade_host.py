#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""RUNDLL.EXE MSPRINT.DLL,RUNDLL_FinishUpgrade in the host.

Active Setup runs this step on the first start of the shell. MSPRINT.DLL is a
16-bit library for Windows 4.0; KRNL386 calls its DLLENTRYPOINT after
LibMain, and that connects the thunk blocks MSPTHK and MSPFTHK to
MSPRINT2.DLL: ThunkConnect16 loads the 32-bit DLL, whose DllMain runs on the
flat stack of the thread and links its blocks to the 16-bit module the
program already runs. RUNDLL_FinishUpgrade then goes through C16ThkSL01 into
MSPRINT2, which asks the spooler through WINSPOOL; the answer lies in the
spooler's HEAP_SHARED heap, which the Win16 process sees because it joins the
session arena. Before, the step ended at C16ThkSL01 with a crash report on
every first start.

    build/analysis-venv/bin/python tools/test_msprint_upgrade_host.py [--no-build]

Results in build/msprint-upgrade/results.json.
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


def main():
    out = neo98.output_directory('msprint-upgrade')
    checks = {}

    def check(name, condition):
        checks[name] = bool(condition)

    drive_c = out / 'drive-c'
    shutil.rmtree(drive_c, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(win98_c.FOLDER.resolve()), str(drive_c)], check=True)
    reports = out / 'reports'
    shutil.rmtree(reports, ignore_errors=True)
    binary = ROOT / 'build/native-app/neo98-host'
    if '--no-build' not in sys.argv:
        binary = build_host()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_MENU='60:quit')
    env.pop('NEO98_HOST_INPUT', None)
    env.pop('NEO98_HOST_ATTACHED', None)
    command = [str(binary), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
               '--system-tree', '--exe', 'C:\\WINDOWS\\RUNDLL.EXE',
               '--command', 'RUNDLL.EXE MSPRINT.DLL,RUNDLL_FinishUpgrade',
               '--drive-c', str(drive_c), '--no-registry-file', '--crash-report-dir', str(reports)]
    log_path = out / 'host.log'
    with log_path.open('w') as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, timeout=300)
    text = log_path.read_text(errors='replace')
    check('MSPRINT DLLENTRYPOINT ran', 'WIN16 library MSPRINT DLLENTRYPOINT' in text)
    check('thunk blocks linked to the running module',
          'FLATTHUNK connect hosted dll16=MSPRINT.DLL name=MspThk_ThunkData16' in text and
          'FLATTHUNK connect hosted dll16=MSPRINT.DLL name=MspFThk_ThunkData16' in text)
    check('16-to-32 thunk reached MSPRINT2', 'FLATTHUNK sl index=' in text)
    check('RUNDLL ended normally', 'NEO98 HOST exited code=0' in text)
    check('no crash report', not reports.exists() or not any(reports.iterdir()))
    report = {'log': str(log_path), 'exit_code': result.returncode, 'checks': checks}
    (out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for name, okay in checks.items():
        print(f"{'ok  ' if okay else 'FAIL'} {name}")
    failed = [name for name, okay in checks.items() if not okay]
    if failed:
        print(f'MSPRINT UPGRADE FAIL: {len(failed)} of {len(checks)} checks; see {log_path}')
        return 1
    print(f'MSPRINT UPGRADE PASS: {len(checks)} checks; {out / "results.json"}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
