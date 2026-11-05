#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""repeated commit-on-fault with FEX and Unicorn. Use serial.py."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from ui_clipboard_test_support import fixture


def guard():
    subprocess.run(['df', '-h', '/System/Volumes/Data'], check=True, stdin=subprocess.DEVNULL)
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard()
    out = ROOT / 'build/repeated-commit-fault'
    out.mkdir(exist_ok=True)
    exe = fixture(out / 'fixture', ROOT / 'tools/repeated_commit_fault_fixture.c',
                  {'KERNEL32': {'ExitProcess': 4, 'VirtualAlloc': 16, 'VirtualFree': 12}})
    for engine in args.cpu or ('fex', 'unicorn'):
        guard()
        active = subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True,
                                text=True, stdin=subprocess.DEVNULL)
        if active.returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active: ' + active.stdout.strip())
        drive = out / f'c-{engine}'
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str((ROOT / 'build/win98-c').resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert drive.is_dir() and not drive.is_symlink()
            guard()
            env = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='10',
                       NEO98_HOST_INPUT='3:key=0d;5:menu=quit', NEO98_HOST_MENU='',
                       NEO98_SETTINGS_DIR=str(out / f'settings-{engine}'),
                       NEO98_LOG_DIR=str(out / f'logs-{engine}'), NEO98_HOST_PASTEBOARD='unique')
            log_path = out / (engine + ('-before' if args.baseline else '-after') + '.log')
            with log_path.open('w') as log:
                process = subprocess.Popen([str(args.host.resolve()), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\' + exe.name,
                    '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(exe.parent), '--no-registry-file',
                    '--quiet', '--offline', '--crash-report-dir', str(out / f'reports-{engine}')],
                    cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise AssertionError(f'{engine}: host deadline exceeded')
            log = log_path.read_text(errors='replace')
            count = log.count('EXCEPTION c0000005 trap 14 ')
            if args.baseline:
                assert count == 8 and 'NEO98 HOST exited code=0 ' not in log, log[-4000:]
                print(f'PASS {engine}: baseline stops after eight repaired page faults', flush=True)
            else:
                assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in log and count == 32, log[-4000:]
                print(f'PASS {engine}: 32 reservations and successful commit-on-fault retries', flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists():
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
