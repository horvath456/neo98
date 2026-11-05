#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""REP MOVSD resumes with completed-iteration registers after SEH.

Run through a shared serial test lock. Uses private APFS C: clones and both CPUs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROOT = Path(__file__).resolve().parents[1]


def guard():
    if shutil.disk_usage(ROOT).free < 8.5 * 1024**3:
        raise RuntimeError('STOP: fewer than 8.5 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    args = parser.parse_args()
    guard()
    out = ROOT / 'build/rep-fault-progress'
    out.mkdir(exist_ok=True)
    exe = out / 'PROBE.EXE'
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-nostdlib', '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp',
                    '-o', str(exe), str(ROOT / 'tools/rep_fault_progress_fixture.c'),
                    '-luser32', '-lkernel32'], check=True, stdin=subprocess.DEVNULL)
    host = args.host.resolve()
    summary = {'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'engines': {}}
    for cpu in args.cpu or ('fex', 'unicorn'):
        guard()
        if subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True,
                          stdin=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError('STOP: another neo98-host is active')
        drive = out / ('c-' + cpu)
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str((ROOT / 'build/win98-c').resolve()) + '/',
                            str(drive)], check=True, stdin=subprocess.DEVNULL)
            assert not drive.is_symlink()
            guard()
            env = dict(os.environ, NEO98_CPU=cpu, NEO98_HOST_DEADLINE='40',
                       NEO98_HOST_INPUT='', NEO98_HOST_MENU='', SDL_AUDIODRIVER='dummy',
                       SDL_VIDEODRIVER='dummy', NEO98_SETTINGS_DIR=str(out / ('settings-' + cpu)),
                       NEO98_LOG_DIR=str(out / ('logs-' + cpu)), NEO98_HOST_PASTEBOARD='unique')
            registry = out / (cpu + '.registry')
            registry.unlink(missing_ok=True)
            log_path = out / (cpu + ('-before.log' if args.baseline else '-after.log'))
            with log_path.open('w') as log:
                process = subprocess.Popen([str(host), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                    '--exe', r'E:\PROBE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(out), '--registry', str(registry), '--quiet', '--offline'],
                    env=env, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=30)
                    raise AssertionError(cpu + ': host deadline exceeded')
            log_text = log_path.read_text()
            assert process.returncode == 0 and 'NEO98 HOST exited code=' in log_text, log_text[-1200:]
            blob=(drive/'REPPROG.BIN').read_bytes()
            code,count=struct.unpack_from('<II',blob)
            rows=list(struct.iter_unpack('<5I',blob[8:]))
            assert f'NEO98 HOST exited code={code} ' in log_text and 'NEO98 HOST deadline' not in log_text, log_text[-1200:]
            summary['engines'][cpu]={'code':code,'count':count,'rows':rows}
            print(cpu, code, count, rows, flush=True)
            if args.baseline and cpu=='fex':
                assert code==12 and count==3 and rows[1][1:4]==rows[0][1:4], rows
            else:
                assert code==0 and count==4 and len(rows)==4, rows
                for offset,esi,edi,ecx,write in rows:
                    assert (esi,edi,ecx,write)==(offset,offset-44,(0x4000-offset)//4,0), rows
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists(): shutil.rmtree(drive)
    (out / ('before.json' if args.baseline else 'after.json')).write_text(json.dumps(summary, indent=2) + '\n')


if __name__ == '__main__':
    main()
