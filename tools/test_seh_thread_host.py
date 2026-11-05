#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""yielding SEH handler and thread isolation with FEX and Unicorn. Use serial.py."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]


def guard():
    subprocess.run(['df', '-h', '/System/Volumes/Data'], check=True, stdin=subprocess.DEVNULL)
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true', help='Use --cpu unicorn: FEX did not reproduce the baseline failure')
    parser.add_argument('--nonlocal', dest='nonlocal_regression', action='store_true', help='Run the existing nested/nonlocal SEH callback regression')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard()
    out = ROOT / ('build/seh-nonlocal-host' if args.nonlocal_regression else 'build/seh-thread')
    out.mkdir(exist_ok=True)
    exe = out / 'SEHTHR.EXE'
    if shutil.disk_usage(ROOT).free < 8.6 * 1024**3:
        raise RuntimeError('STOP build: fewer than 8.6 GiB free')
    objects = []
    if args.nonlocal_regression:
        for name in ('seh_callback_leave_probe.S', 'kernel_exceptions_probe.S'):
            obj = out / (name + '.o')
            subprocess.run(['clang', '--target=i686-w64-windows-gnu', '-c',
                            str(ROOT / 'tools' / name), '-o', str(obj)],
                           check=True, stdin=subprocess.DEVNULL)
            objects.append(str(obj))
    source = 'seh_callback_leave_fixture.c' if args.nonlocal_regression else 'seh_thread_fixture.c'
    subprocess.run(['i686-w64-mingw32-gcc', str(ROOT / 'tools' / source),
                    *objects, '-O1', '-nostartfiles', '-Wl,-e,_mainCRTStartup', '-mwindows',
                    '-luser32', '-lkernel32', '-o', str(exe)], check=True, stdin=subprocess.DEVNULL)
    for engine in args.cpu or ('fex', 'unicorn'):
        guard()
        active = subprocess.run(['ps', '-axo', 'pid=,comm='], capture_output=True,
                                text=True, stdin=subprocess.DEVNULL)
        active.check_returncode()
        hosts = [row for row in active.stdout.splitlines() if row.split() and Path(row.split()[-1]).name in ('neo98-host', 'sweep-before-b6279')]
        if hosts:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active: ' + '; '.join(hosts))
        drive = out / f'c-{engine}'
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str((ROOT / 'build/win98-c').resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert drive.is_dir() and not drive.is_symlink()
            guard()
            env = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='20',
                       NEO98_HOST_INPUT='15:menu=quit', NEO98_HOST_MENU='',
                       NEO98_SETTINGS_DIR=str(out / f'settings-{engine}'),
                       NEO98_LOG_DIR=str(out / f'logs-{engine}'), NEO98_HOST_PASTEBOARD='unique')
            log_path = out / (engine + ('-before' if args.baseline else '-after') + '.log')
            with log_path.open('w') as log:
                process = subprocess.Popen([str(args.host.resolve()), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\' + exe.name,
                    '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive', 'E=' + str(exe.parent), '--no-registry-file',
                    '--quiet', '--offline', '--crash-report-dir', str(out / f'reports-{engine}')],
                    cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise AssertionError(f'{engine}: host deadline exceeded')
            log = log_path.read_text(errors='replace')
            if args.baseline:
                assert 'Unexpected callback return' in log, log[-4000:]
                print(f'PASS {engine}: reproduced callback loss after switching out of a handler', flush=True)
            else:
                assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in log, log[-4000:]
                print(f'PASS {engine}: ' + ('existing nested/nonlocal SEH callback regression' if args.nonlocal_regression else '40 yielding handlers return after a second thread runs'), flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists():
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
