#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""BIOS reads with FEX and Unicorn. Invoke through serial.py."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]  # also under python -I
import native_app
import neo98


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', action='store_true', help='verify the original missing BIOS mapping')
    args = parser.parse_args()
    out = ROOT / 'build/bios-memory'
    out.mkdir(exist_ok=True)
    env = neo98.tool_environment()
    def run(*command):
        subprocess.run(list(map(str, command)), env=env, check=True, stdin=subprocess.DEVNULL)
    def guard():
        if shutil.disk_usage(ROOT).free < 8 * 1024**3:
            raise RuntimeError('STOP: fewer than 8 GiB free')
    guard()
    (out / 'kernel.def').write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nVirtualQuery@12\nExitProcess@4\n')
    run('llvm-dlltool', '-m', 'i386', '-k', '-d', out / 'kernel.def', '-l', out / 'kernel.lib')
    run('clang', '--target=i686-pc-windows-msvc', '-O1', '-march=pentium', '-mno-sse', '-mno-sse2',
        '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-c',
        ROOT / 'tools/bios_memory_fixture.c', '-o', out / 'fixture.obj')
    run('lld-link', '/entry:mainCRTStartup', '/subsystem:windows,4.0', '/nodefaultlib',
        '/machine:x86', '/timestamp:0', '/out:' + str(out / 'BIOS.EXE'), out / 'fixture.obj', out / 'kernel.lib')
    for engine in native_app.test_engines():
        run('df', '-h', ('/System/Volumes/Data' if __import__('sys').platform == 'darwin' else '/'))
        guard()
        if subprocess.run(__import__('test_support').other_hosts_command(), capture_output=True).returncode == 0:
            __import__('test_support').skip('exclusive host access is unavailable: another neo98-host is active')
        drive = out / ('c-' + engine)
        assert not drive.exists() and not drive.is_symlink()
        process = None
        try:
            run('cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()) + '/', drive)
            assert not drive.is_symlink()
            runtime = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                           NEO98_HOST_INPUT='', NEO98_HOST_MENU='', NEO98_HOST_DEADLINE='30',
                           NEO98_SETTINGS_DIR=str(out / ('settings-' + engine)),
                           NEO98_LOG_DIR=str(out / ('logs-' + engine)))
            logfile = out / (('before-' if args.before else 'after-') + engine + '.log')
            with logfile.open('w') as log:
                process = subprocess.Popen([str(ROOT / 'build/native-app/neo98-host'), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\BIOS.EXE', '--quiet', '--offline',
                    '--system-tree', str(drive), '--drive-c', str(drive), '--drive-readonly', 'E=' + str(out),
                    '--no-registry-file'], env=runtime, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=60)
                    raise
            text = logfile.read_text(errors='replace')
            expected = 1 if args.before else 0
            assert process.returncode == 0 and f'NEO98 HOST exited code={expected} ' in text, text[-2500:]
            assert '\nExitProcess\n' in text and 'NEO98 HOST deadline' not in text, text[-2500:]
            print(f'PASS {engine}: ' + ('missing BIOS reproduced' if args.before else
                  '64 KiB BIOS readable; firmware bytes, MEM_FREE and null-page fault checked'), flush=True)
        finally:
            if process is None or process.poll() is not None:
                if drive.exists():
                    assert not drive.is_symlink()
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
