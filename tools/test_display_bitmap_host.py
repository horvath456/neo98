#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check cached display bitmaps and solid colors after a depth change.

Run through the shared serial test runner. Each CPU uses a private C: clone.
An optional reference file compares the exact rows with an original system.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import signal
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import disk_room
import neo98
import win98_c


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/display-bitmap-host')
    parser.add_argument('--engine', action='append', choices=('fex', 'unicorn'))
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    disk_room.require(8.0)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    exe = out / 'PROBE.EXE'
    environment = neo98.tool_environment()
    subprocess.run(['i686-w64-mingw32-gcc', '-O1', '-ffreestanding', '-fno-builtin', '-nostdlib',
                    '-Wl,--entry,_mainCRTStartup', '-Wl,--no-insert-timestamp', '-o', str(exe),
                    str(ROOT / 'tools/display_bitmap_fixture.c'), '-luser32', '-lgdi32', '-lkernel32'],
                   env=environment, check=True, stdin=subprocess.DEVNULL)
    summary = {'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'engines': {}}
    for engine in args.engine or ('fex', 'unicorn'):
        disk_room.require(8.0)
        drive = out / ('c-' + engine)
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str(win98_c.use_pristine()), str(drive)], check=True)
            env = dict(environment, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                       NEO98_CPU=engine, NEO98_HOST_INPUT='', NEO98_HOST_MENU='',
                       NEO98_HOST_DEADLINE='40', NEO98_HOST_PASTEBOARD='unique',
                       NEO98_SETTINGS_DIR=str(out / ('settings-' + engine)),
                       NEO98_LOG_DIR=str(out / ('logs-' + engine)))
            with (out / (engine + '.log')).open('w') as log:
                process = subprocess.Popen([str(args.binary.resolve()), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                    '--exe', r'E:\PROBE.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(out), '--no-registry-file', '--quiet', '--offline'],
                    env=env, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    try:
                        process.wait(timeout=30)
                    except subprocess.TimeoutExpired:
                        process.send_signal(signal.SIGQUIT)
                        process.wait(timeout=30)
                    raise AssertionError(engine + ': host deadline exceeded')
            blob = (drive / 'UPDRGN.BIN').read_bytes()
            code, = struct.unpack_from('<I', blob)
            rows = [list(row) for row in struct.iter_unpack('<3I', blob[4:])]
            summary['engines'][engine] = {'code': code, 'checks': len(rows), 'rows': rows}
            (out / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(engine, code, len(rows), [row for row in rows if row[1] != row[2]], flush=True)
            assert process.returncode == 0 and code == 0 and len(rows) == 25
            assert all(got == want for _, got, want in rows)
            if args.reference:
                reference = json.loads(args.reference.read_text())
                assert reference['fixture_sha256'] == summary['fixture_sha256']
                assert reference['rows'] == rows
        finally:
            if process is None or process.poll() is not None:
                shutil.rmtree(drive, ignore_errors=True)


if __name__ == '__main__':
    main()
