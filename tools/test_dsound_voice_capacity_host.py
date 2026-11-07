#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""advertised hardware voices, simultaneous playback and software spillover.

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
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--repeat', type=int, default=1, help='Independent starts per CPU; any failure fails the run')
    args = parser.parse_args()
    if args.repeat < 1: parser.error('--repeat must be positive')
    guard()
    out = ROOT / 'build/dsound-voice-capacity'
    out.mkdir(exist_ok=True)
    exe = out / 'PROBE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-nostdlib', '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp',
                    '-o', str(exe), str(ROOT / 'tools/dsound_voice_capacity_fixture.c'),
                    '-ldsound', '-luser32', '-lkernel32'], check=True, stdin=subprocess.DEVNULL)
    host = args.host.resolve()
    summary = {'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'engines': {}}
    base = out
    for iteration, cpu in ((n, cpu) for n in range(args.repeat) for cpu in (args.cpu or ('fex', 'unicorn'))):
        out = base / f'run-{iteration + 1:02d}-{cpu}'
        out.mkdir(exist_ok=True)
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
                       NEO98_HOST_INPUT='20:menu=report', NEO98_HOST_MENU='', SDL_AUDIODRIVER='dummy',
                       SDL_VIDEODRIVER='dummy', NEO98_SETTINGS_DIR=str(out / ('settings-' + cpu)),
                       NEO98_LOG_DIR=str(out / ('logs-' + cpu)), NEO98_HOST_PASTEBOARD='unique')
            log_path = out / (cpu + ('-before.log' if args.baseline else '-after.log'))
            with log_path.open('w') as log:
                process = subprocess.Popen([str(host), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                    '--exe', r'E:\PROBE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(base), '--crash-report-dir', str(out / 'reports'), '--no-registry-file', '--quiet', '--offline'],
                    env=env, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=30)
                    raise AssertionError(cpu + ': host deadline exceeded')
            # Diagnose startup deadlines before interpreting a partial report.
            code = 1 if args.baseline else 0
            log_text = log_path.read_text()
            assert process.returncode == 0 and f'NEO98 HOST exited code={code} ' in log_text, (cpu, log_text[-2000:])
            rows = list(struct.iter_unpack('<II', (drive / 'VOICES.BIN').read_bytes()))
            (out / (cpu + ('-before-rows.json' if args.baseline else '-after-rows.json'))).write_text(json.dumps(rows) + '\n')
            values = dict(rows)
            failures = [row for row in rows if row[0] < 10000 and row[1]]
            assert values[10003] == 192, values[10003]
            if args.baseline:
                assert values[10000] > 63 and any(1000 <= n < 1256 for n, v in failures), failures[:5]
            else:
                assert not failures, failures[:10]
                assert values[10000] == values[10001] == 63, (values[10000], values[10001])
            summary['engines'][f'{iteration + 1}:{cpu}'] = {'rows': len(rows), 'failures': len(failures),
                                       'first_failures': failures[:5], 'advertised_voices': values[10000], 'public_buffers': values[10003]}
            print('PASS', iteration + 1, cpu, 'advertised voices exceed playback capacity' if args.baseline else
                  '63 hardware voices play together; 192 public buffers succeed', flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists(): shutil.rmtree(drive)
    (base / ('before.json' if args.baseline else 'after.json')).write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
