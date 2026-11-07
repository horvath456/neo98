#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""original MSVCRT setjmp/longjmp on main and worker threads.

Run under a shared serial test lock with an already built host.
"""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path
import native_app
from ui_clipboard_test_support import ROOT, fixture, machine


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    args = parser.parse_args()
    out = ROOT / 'build/thread-setjmp'
    exe = fixture(out / 'fixture', ROOT / 'tools/thread_setjmp_fixture.c', {
        'KERNEL32': {'ExitProcess': 4, 'LoadLibraryA': 4, 'GetProcAddress': 8,
                     'CreateThread': 24, 'WaitForSingleObject': 8,
                     'GetExitCodeThread': 8, 'CloseHandle': 4,
                     'SetUnhandledExceptionFilter': 4, 'RaiseException': 16}},
                  defines=('-fno-omit-frame-pointer',))
    results = {'host': str(args.host.resolve()),
               'host_sha256': hashlib.sha256(args.host.read_bytes()).hexdigest(),
               'fixture_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'tests': {}}
    for cpu in native_app.test_engines():
        os.environ['NEO98_CPU'] = cpu
        folder = out / (cpu + ('-before' if args.baseline else '-after'))
        with machine(folder) as drive:
            assert not drive.is_symlink()
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       NEO98_HOST_DEADLINE='20', NEO98_HOST_INPUT='8:menu=quit',
                       NEO98_SETTINGS_DIR=str(folder / 'settings'),
                       NEO98_LOG_DIR=str(folder / 'logs'))
            command = [str(args.host.resolve()), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
                       '--exe', r'E:\TEST.EXE', '--quiet', '--system-tree', str(drive),
                       '--drive-c', str(drive), '--registry', str(folder / 'c.registry'),
                       '--drive-readonly', 'E=' + str(exe.parent)]
            with (folder / 'host.log').open('w') as output:
                process = subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=40)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise
            log = (folder / 'host.log').read_text(errors='replace')
            assert code == 0, (cpu, log[-3000:])
            if args.baseline:
                assert 'EXCEPTION c0000005 trap 14 at 7801e800' in log, log[-3000:]
            else:
                assert 'NEO98 HOST exited code=0 ' in log, log[-3000:]
                assert 'EXCEPTION c0000005' not in log, log[-3000:]
            results['tests'][cpu] = 'expected access violation' if args.baseline else 'PASS'
            print(cpu, results['tests'][cpu], flush=True)
    (out / ('before.json' if args.baseline else 'results.json')).write_text(
        json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
