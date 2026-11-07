#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""unchanged demo texture dispatch in PE guests, FEX and Unicorn.

Run through a shared serial test lock. Only dimension, DirectDraw and upload providers are supplied;
no installed game or original DLL is patched. Each run uses a private C: clone.
"""
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
import native_app
SHA = 'd1d91a98578ba50233bdfdc56a1e171029e7df6dbf0860e0a47c3e9f77b990f7'


def guard():
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise RuntimeError('STOP: fewer than 8 GiB free')


def main():
    parser = ExternalInputParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    guard()
    data = args.exe.read_bytes()
    assert hashlib.sha256(data).hexdigest() == SHA
    pe = pefile.PE(data=data)
    original = pe.get_memory_mapped_image()[0x117f00:0x118128]
    out = ROOT / 'build/descent-texture-dispatch'
    out.mkdir(exist_ok=True)
    (out / 'descent-dispatch-original.h').write_text('static const unsigned char original_dispatch[]={' +
                                               ','.join(str(b) for b in original) + '};\n')
    definition, library = out / 'KERNEL32.def', out / 'KERNEL32.lib'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\nVirtualProtect@16\nExitProcess@4\n')
    env = neo98.tool_environment()
    def run(*command):
        subprocess.run(list(map(str, command)), env=env, stdin=subprocess.DEVNULL, check=True)
    run('llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library)
    obj, exe = out / 'fixture.obj', out / 'D3DISPAT.EXE'
    run('clang', '--target=i686-pc-windows-msvc', '-O1', '-march=pentium', '-mno-sse', '-mno-sse2', '-ffreestanding', '-fno-builtin',
        '-fno-stack-protector', '-I', out, '-c', ROOT / 'tools/descent_texture_dispatch_fixture.c', '-o', obj)
    run('lld-link', '/entry:mainCRTStartup', '/subsystem:windows,4.0', '/nodefaultlib',
        '/machine:x86', '/base:0x400000', '/timestamp:0', '/out:' + str(exe), obj, library)
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
                           NEO98_HOST_INPUT='', NEO98_HOST_MENU='', NEO98_HOST_DEADLINE='180',
                           NEO98_SETTINGS_DIR=str(out / ('settings-' + engine)),
                           NEO98_LOG_DIR=str(out / ('logs-' + engine)))
            with (out / (engine + '.log')).open('w') as log:
                process = subprocess.Popen([str(ROOT / 'build/native-app/neo98-host'), '-',
                    'C:\\WINDOWS\\FONTS\\VGAFIX.FON', '--exe', 'E:\\D3DISPAT.EXE', '--quiet', '--offline',
                    '--system-tree', str(drive), '--drive-c', str(drive), '--drive-readonly', 'E=' + str(out),
                    '--no-registry-file'], env=runtime, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
                try:
                    process.wait(timeout=210)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=60)
                    raise
            text = (out / (engine + '.log')).read_text(errors='replace')
            assert process.returncode == 0 and 'NEO98 HOST exited code=0 ' in text, text[-2500:]
            assert '\nExitProcess\n' in text and 'NEO98 HOST deadline' not in text, text[-2500:]
            print(f'PASS {engine}: six unchanged texture-dispatch cases; bump-map table/type defect reproduced', flush=True)
        finally:
            if process is None or process.poll() is not None:
                if drive.exists():
                    assert not drive.is_symlink()
                    shutil.rmtree(drive)


if __name__ == '__main__':
    main()
