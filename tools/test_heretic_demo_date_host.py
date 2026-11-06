#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""unchanged demo date guard in PE guests, FEX and Unicorn.

Run through a shared serial test lock. Only oracle time/error providers are supplied;
no installed game or original DLL is patched. Each run uses a private C: clone.
"""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
from test_support import ExternalInputParser
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import pefile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
SHA = '29b27cc8cf013d16892f902b4de1c3fba3d743bb11db4602c72697901380be86'


def guard():
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = ExternalInputParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    args = parser.parse_args()
    guard()
    data = args.dll.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA
    pe = pefile.PE(data=data)
    original = pe.get_memory_mapped_image()[0x2df50:0x2df81]
    out = ROOT / 'build/heretic-demo-date'
    out.mkdir(exist_ok=True)
    (out / 'heretic-date-original.h').write_text('static const unsigned char original_guard[]={' +
                                               ','.join(str(b) for b in original) + '};\n')
    definition, library = out / 'KERNEL32.def', out / 'KERNEL32.lib'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nVirtualAlloc@16\nExitProcess@4\n')
    env = neo98.tool_environment()
    def run(*command):
        subprocess.run(list(map(str, command)), env=env, stdin=subprocess.DEVNULL, check=True)
    run('llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library)
    obj, exe = out / 'fixture.obj', out / 'H2DATE.EXE'
    run('clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
        '-fno-stack-protector', '-I', out, '-c', ROOT / 'tools/heretic_demo_date_fixture.c', '-o', obj)
    run('lld-link', '/entry:mainCRTStartup', '/subsystem:windows,4.0', '/nodefaultlib',
        '/machine:x86', '/timestamp:0', '/out:' + str(exe), obj, library)
    for engine in ('fex', 'unicorn'):
        run('df', '-h', ('/System/Volumes/Data' if __import__('sys').platform == 'darwin' else '/'))
        guard()
        if subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True).returncode == 0:
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
            with (out / (engine + '.log')).open('w') as log:
                process = subprocess.Popen([str(ROOT / 'build/native-app/neo98-host'), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\H2DATE.EXE', '--quiet', '--offline',
                    '--system-tree', str(drive), '--drive-c', str(drive), '--drive-readonly', 'E=' + str(out),
                    '--no-registry-file'], env=runtime, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=20)
                    raise
            text = (out / (engine + '.log')).read_text(errors='replace')
            assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in text, text[-2500:]
            print(f'PASS {engine}: seven unchanged demo date-guard cases', flush=True)
        finally:
            if process is None or process.poll() is not None:
                if drive.exists():
                    assert not drive.is_symlink()
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
