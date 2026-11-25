#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""An Explorer with a command line in a session that has no shell yet.

Explorer becomes the shell when no shell window is there, and the shell takes
its command line for a file to open. neo98-kms runs two sessions that start
with another program than the desktop, over a private copy of drive C: whose
first start after setup is done:

  explore  the first program is EXPLORER.EXE /n,/e,C:\\ : the desktop comes up
           and an Explorer window of C:\\ opens, without "Cannot find the
           file '/n,/e,C:\\'"
  control  the first program is CONTROL.EXE, which opens the Control Panel
           through an Explorer with /idlist: the Control Panel opens and lists
           its applets, without "Cannot find the file '/idlist,:...'"

    build/analysis-venv/bin/python tools/test_explorer_command_host.py [--no-build] [--kms KMS]
"""
import sys as _sys
from test_support import skip as _skip
if _sys.platform != 'linux':
    _skip('this test requires the Linux host')
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
import native_app  # noqa: E402
import win98_c  # noqa: E402

WORK = ROOT / 'build/explorer-command-test'


def copy(source, target):
    subprocess.run(['cp', '-a', '--reflink=auto', str(source), str(target)], check=True)
    if win98_c.journal(source).is_file():
        shutil.copy2(win98_c.journal(source), win98_c.journal(target))


def session(binary, drive, folder, program, command, steps, seconds):
    folder.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', NEO98_KMS_OUTPUT='image:1024x768',
               NEO98_HOST_INPUT=steps.replace('{out}', str(folder)))
    for variable in ('DISPLAY', 'WAYLAND_DISPLAY', 'NEO98_KMS_CARD'):
        env.pop(variable, None)
    arguments = [str(binary), '--drive-c', str(drive), '--desktop', '1024x768', '--exe', program,
                 *(['--command', command] if command else []), '--time-limit', str(seconds)]
    result = subprocess.run(arguments, cwd=ROOT, env=env, capture_output=True, text=True, timeout=seconds + 60)
    text = result.stdout + result.stderr
    (folder / 'log.txt').write_text(text)
    return text


def run(binary, prepared, name, program, command, window):
    """One session over a copy of the prepared drive, until the window is there."""
    folder = WORK / name
    drive = folder / 'c'
    folder.mkdir(parents=True)
    copy(prepared, drive)
    try:
        return session(binary, drive, folder, program, command, f'0:wait={window};4:shot={{out}}/window.png;5:quit=1',
                       120)
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        win98_c.journal(drive).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--kms', type=Path, default=ROOT / 'build/native-app/neo98-kms')
    args = parser.parse_args()
    if not native_app.native_x64():
        raise SystemExit('neo98-kms is a Linux x64 host.')
    if not args.no_build:
        native_app.build_kms_host()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    # The first start after setup runs its RunOnce work before the desktop
    # comes up; it runs once, with the desktop, and both sessions start from
    # the drive it leaves.
    prepared = WORK / 'prepared'
    copy(win98_c.use_pristine(), prepared)
    try:
        first = session(args.kms, prepared, WORK / 'first', 'C:\\WINDOWS\\EXPLORER.EXE', None,
                        '0:wait=Shell_TrayWnd;10:quit=1', 600)
        if 'NEO98 KMS input wait=Shell_TrayWnd' not in first:
            raise SystemExit(f'The first start did not bring the desktop up; log in {WORK / "first"}')
        explore = run(args.kms, prepared, 'explore', 'C:\\WINDOWS\\EXPLORER.EXE',
                      'C:\\WINDOWS\\EXPLORER.EXE /n,/e,C:\\', 'ExploreWClass')
        control = run(args.kms, prepared, 'control', 'C:\\WINDOWS\\CONTROL.EXE', None, 'CabinetWClass')
    finally:
        shutil.rmtree(prepared, ignore_errors=True)
        win98_c.journal(prepared).unlink(missing_ok=True)
    missing = 'text=Cannot find the file'
    checks = {
        'explore: the desktop comes up': 'class=Shell_TrayWnd' in explore,
        'explore: an Explorer window of C:\\ opens': 'NEO98 KMS input wait=ExploreWClass' in explore,
        "explore: no box says it cannot find '/n,/e,C:\\'": missing not in explore,
        'control: the Control Panel opens': 'NEO98 KMS input wait=CabinetWClass' in control,
        'control: it lists the applets': 'Control Panel driver applets' in control,
        "control: no box says it cannot find '/idlist,...'": missing not in control,
    }
    for name, passed in checks.items():
        print(('ok  ' if passed else 'FAIL'), name, flush=True)
    print(f'Pictures and logs in {WORK}', flush=True)
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit(f'{len(failed)} of {len(checks)} checks failed')
    print(f'explorer command: all {len(checks)} checks passed', flush=True)


if __name__ == '__main__':
    main()
