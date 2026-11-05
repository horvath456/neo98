#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""real PE children with CREATE_SEPARATE_WOW_VDM, FEX and Unicorn.

Run through a shared serial test lock. Each engine receives a private APFS C: clone.
No timeout path sends SIGKILL; the host has its own deadline.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

IMPORTS = {'ExitProcess': 4, 'GetCommandLineA': 0, 'GetModuleFileNameA': 12,
           'GetCurrentProcessId': 0, 'CreateProcessA': 40, 'WaitForSingleObject': 8,
           'ResumeThread': 4, 'GetExitCodeProcess': 8, 'GetLastError': 0, 'CloseHandle': 4}


def guard():
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expect-rejection', action='store_true',
                        help='Check the unfixed host rejects the first 0x800 launch')
    parser.add_argument('--cpu', choices=('fex', 'unicorn'), action='append')
    args = parser.parse_args()
    guard()
    out = ROOT / 'build/process-wow-flag'
    out.mkdir(exist_ok=True)
    env = neo98.tool_environment()
    definition, library = out / 'KERNEL32.def', out / 'KERNEL32.lib'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n' +
                          ''.join(f'{name}@{size}\n' for name, size in IMPORTS.items()))
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool
    subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                   env=env, check=True, stdin=subprocess.DEVNULL)
    obj, exe = out / 'fixture.obj', out / 'WOWFLAG.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-stack-protector', '-fno-builtin', '-c', str(ROOT / 'tools/process_wow_flag_fixture.c'),
                    '-o', str(obj)], env=env, check=True, stdin=subprocess.DEVNULL)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/nodefaultlib',
                    '/machine:x86', '/timestamp:0', '/out:' + str(exe), str(obj), str(library)],
                   env=env, check=True, stdin=subprocess.DEVNULL)
    binary = ROOT / 'build/native-app/neo98-host'
    reference = Path(os.environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c').resolve()
    for engine in args.cpu or ('fex', 'unicorn'):
        subprocess.run(['df', '-h', '/System/Volumes/Data'], check=True,
                       stdin=subprocess.DEVNULL)
        guard()
        active = subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True, text=True,
                                stdin=subprocess.DEVNULL)
        if active.returncode == 0:
            raise RuntimeError('STOP: another neo98-host is active: ' + active.stdout.strip())
        drive = out / f'c-{engine}'
        assert not drive.exists() and not drive.is_symlink(), drive
        process = None
        try:
            subprocess.run(['cp', '-c', '-R', str(reference) + '/', str(drive)], check=True,
                           stdin=subprocess.DEVNULL)
            assert drive.is_dir() and not drive.is_symlink()
            guard()
            runtime = dict(os.environ, NEO98_CPU=engine, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                           NEO98_HOST_INPUT='', NEO98_HOST_MENU='', NEO98_HOST_DEADLINE='40',
                           NEO98_SETTINGS_DIR=str(out / f'settings-{engine}'),
                           NEO98_LOG_DIR=str(out / f'logs-{engine}'), NEO98_HOST_PASTEBOARD='unique')
            with (out / f'{engine}.log').open('w') as log:
                process = subprocess.Popen([str(binary), '-', 'C:\\WINDOWS\\FONTS\\VGAFIX.FON',
                    '--exe', 'E:\\WOWFLAG.EXE', '--system-tree', str(drive), '--drive-c', str(drive),
                    '--drive-readonly', 'E=' + str(out), '--no-registry-file', '--quiet', '--offline',
                    '--crash-report-dir', str(out / f'reports-{engine}')], cwd=ROOT, env=runtime,
                    stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=30)
                    raise AssertionError(f'{engine}: host deadline exceeded')
            text = (out / f'{engine}.log').read_text(errors='replace')
            expected = 11 if args.expect_rejection else 0
            assert process.returncode == 0 and f'NEO98 HOST exited code={expected} ' in text, text[-4000:]
            if args.expect_rejection:
                assert 'CreateProcess rejected:' in text and 'flags=00000800' in text
                print(f'PASS {engine}: reproduced 0x800 rejection, guest exit 11', flush=True)
            else:
                print(f'PASS {engine}: five child launches, suspension, exit codes and rejected inputs', flush=True)
        finally:
            if process is None or process.poll() is not None:
                assert not drive.is_symlink()
                if drive.exists():
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
