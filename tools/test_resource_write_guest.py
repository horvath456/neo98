#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""resource-write retry through real SEH and public memory APIs."""
if __name__ == '__main__':
    import sys as _sys
    if _sys.platform != 'darwin':
        print('SKIP: compares the FEX and Unicorn engines of macOS')  # macOS only; skipped elsewhere
        _sys.exit(77)
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import pefile
from native_app import build_host
from ui_clipboard_test_support import fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', type=Path)
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/resource-write-guest'
    out.mkdir(exist_ok=True)
    imports = {'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4, 'VirtualQuery': 12,
               'VirtualProtect': 16, 'VirtualAlloc': 16, 'SetUnhandledExceptionFilter': 4,
               'UnhandledExceptionFilter': 4, 'SetErrorMode': 4,
               'LoadLibraryA': 4, 'LoadLibraryExA': 12, 'FreeLibrary': 4,
               'CreateThread': 24, 'WaitForSingleObject': 8, 'GetExitCodeThread': 8,
               'RaiseException': 16}}
    exe = fixture(out / 'fixture', ROOT / 'tools/resource_write_fixture.c', imports)
    pe = pefile.PE(str(exe))
    section = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.testres')
    section.Characteristics = 0x40000040
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[2]
    directory.VirtualAddress = section.VirtualAddress + 0x1100
    directory.Size = 0x2200
    pe.write(str(exe))
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    pe.FILE_HEADER.Characteristics |= 0x2000
    pe.OPTIONAL_HEADER.ImageBase = 0x44000000
    pe.OPTIONAL_HEADER.AddressOfEntryPoint = 0
    pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].VirtualAddress = 0
    pe.OPTIONAL_HEADER.DATA_DIRECTORY[1].Size = 0
    dll = out / 'RSRC.DLL'
    pe.write(str(dll))
    dll_digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    host = (args.host or build_host()).resolve()
    # Keep the original C: out of every host command, even with --ram-drive.
    temporary = tempfile.TemporaryDirectory(prefix='private-c-', dir=out)
    drive = Path(temporary.name) / 'c'
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str((__import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or __import__('pathlib').Path(__import__('os').environ.get('NEO98_WIN98_ROOT') or ROOT / 'build/win98-c'))).resolve()), str(drive)], check=True)
    assert not drive.is_symlink()
    try:
        results = []
        for cpu in ('fex', 'unicorn'):
            suffix = cpu + ('-before' if args.baseline else '')
            env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_CPU=cpu,
                       NEO98_HOST_DEADLINE='12', NEO98_HOST_INPUT='3:key=0d;5:menu=quit',
                       NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
            command = [str(host), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                       '--exe', str(exe), '--quiet', '--system-tree', str(drive),
                       '--ram-drive', '--no-registry-file', '--drive-readonly', 'D=' + str(out)]
            with (out / (suffix + '.log')).open('w') as log:
                process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
                try: code = process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    process.terminate(); process.wait(timeout=20); raise
            text = (out / (suffix + '.log')).read_text(errors='replace')
            assert code == 0, (cpu, code, text[-3000:])
            passed = 'NEO98 HOST exited code=0' in text
            assert passed != args.baseline, (cpu, text[-5000:])
            if args.baseline: assert 'EXCEPTION c0000005' in text, text[-3000:]
            assert hashlib.sha256(exe.read_bytes()).hexdigest() == digest
            assert hashlib.sha256(dll.read_bytes()).hexdigest() == dll_digest
            results.append({'cpu': cpu, 'status': 'EXPECTED_FAILURE' if args.baseline else 'PASS'})
            print(cpu, results[-1]['status'], flush=True)
    finally:
        temporary.cleanup()
    (out / ('before.json' if args.baseline else 'results.json')).write_text(json.dumps({
        'host_sha256': hashlib.sha256(host.read_bytes()).hexdigest(), 'exe_sha256': digest,
        'tests': results}, indent=2) + '\n')


if __name__ == '__main__': main()
