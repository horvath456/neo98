#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""257 queued wave headers, reset, playback and complete notifications.

Run through a shared serial test lock. Uses private APFS C: clones and both CPUs.
"""
if __name__ == '__main__':
    import shutil as _shutil, sys as _sys
    _missing = [_t for _t in ['i686-w64-mingw32-gcc'] if not _shutil.which(_t)]
    if _missing:  # this test builds its fixture with them; docs/BUILDING.md
        print('SKIP: needs ' + ', '.join(_missing))
        _sys.exit(77)
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def guard():
    if shutil.disk_usage(ROOT).free < 8.5 * 1024**3:
        raise RuntimeError('STOP: fewer than 8.5 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard()
    out = ROOT / 'build/wave-queue'
    out.mkdir(exist_ok=True)
    exe = out / 'PROBE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-nostdlib', '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp',
                    '-o', str(exe), str(ROOT / 'tools/wave_queue_fixture.c'),
                    '-lwinmm', '-lkernel32'], check=True, stdin=subprocess.DEVNULL)
    host = ROOT / 'build/native-app/neo98-host'
    summary = {'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'engines': {}}
    for cpu in args.cpu or ('fex', 'unicorn'):
        guard()
        if subprocess.run(__import__('test_support').other_hosts_command(), capture_output=True,
                          stdin=subprocess.DEVNULL).returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
        drive = out / ('c-' + cpu)
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert not drive.is_symlink()
            guard()
            env = dict(os.environ, NEO98_CPU=cpu, NEO98_HOST_DEADLINE='40',
                       NEO98_HOST_INPUT='', NEO98_HOST_MENU='', SDL_AUDIODRIVER='dummy',
                       SDL_VIDEODRIVER='dummy', NEO98_SETTINGS_DIR=str(out / ('settings-' + cpu)),
                       NEO98_LOG_DIR=str(out / ('logs-' + cpu)), NEO98_HOST_PASTEBOARD='unique')
            log_path = out / (cpu + ('-before.log' if args.baseline else '-after.log'))
            with log_path.open('w') as log:
                process = subprocess.Popen([str(host), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                    '--exe', r'E:\PROBE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(out), '--no-registry-file', '--quiet', '--offline'],
                    env=env, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=30)
                    raise AssertionError(cpu + ': host deadline exceeded')
            rows = list(struct.iter_unpack('<II', (drive / 'WAVEQ.BIN').read_bytes()))
            failures = [row for row in rows if row[1]]
            assert rows[-1] == (21, 0), rows[-5:]
            if args.baseline:
                assert dict(rows)[2064] == 4 and dict(rows)[2063] == 0, failures[:5]
            else:
                assert not failures, failures[:10]
            code = 1 if args.baseline else 0
            assert process.returncode == 0 and f'NEO98 HOST exited code={code} ' in log_path.read_text(), cpu
            summary['engines'][cpu] = {'rows': len(rows), 'failures': len(failures),
                                       'first_failures': failures[:5]}
            print('PASS', cpu, '64-header rejection reproduced' if args.baseline else
                  '257 headers, reset and playback callbacks exactly once', flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists(): shutil.rmtree(drive)
    (out / ('before.json' if args.baseline else 'after.json')).write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
