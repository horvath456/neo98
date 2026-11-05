#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pending exposure must paint before an outline drag locks the desktop.

Run one case under serial.py. The source drive is read only; the host gets a
private APFS clone. --native tests AppKit input and briefly opens guest windows.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

IMPORTS = {
    'KERNEL32': {'GetModuleHandleA': 4, 'ExitProcess': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'GetMessageA': 16, 'DispatchMessageA': 4, 'BeginPaint': 8,
               'EndPaint': 8, 'GetClientRect': 8, 'FillRect': 12},
    'GDI32': {'GetClipBox': 8, 'CreateSolidBrush': 4},
}


def fixture(out):
    env = neo98.tool_environment()
    libraries = []
    for dll, names in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in names.items()))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition),
                        '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    obj, exe = out / 'fixture.obj', out / 'EXPOSE.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
                    '-fno-builtin', '-fno-stack-protector', '-c',
                    str(ROOT / 'tools/host_seamless_exposure_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libraries],
                   env=env, check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--engine', choices=('fex', 'unicorn'), default='fex')
    parser.add_argument('--mode', choices=('windows', 'layer', 'windowed'), default='windows')
    parser.add_argument('--native', action='store_true')
    parser.add_argument('--resize', action='store_true', help='resize the background to 660 by 470')
    args = parser.parse_args()
    if args.native and args.mode != 'windows':
        parser.error('--native requires --mode windows')
    if shutil.disk_usage(ROOT).free < 8 * 1024**3:
        raise SystemExit('Less than 8 GiB free; no test started')
    case = args.engine + '-' + args.mode
    case += '-native' if args.native else ''
    case += '-resize' if args.resize else ''
    out = neo98.output_directory('seamless-exposure') / case
    out.mkdir(parents=True, exist_ok=True)
    exe = fixture(out)
    drive = out / 'c'
    if drive.exists():
        raise SystemExit(f'Previous private drive still exists: {drive}')
    subprocess.run(['cp', '-c', '-R', str(args.source.resolve()), str(drive)], check=True)
    assert not drive.is_symlink() and drive.resolve() != args.source.resolve()
    shot = out / 'after.ppm'
    shot.unlink(missing_ok=True)
    down, move, up = ('nspress', 'nsmove', 'nsrelease') if args.native else ('down', 'move', 'up')
    start, end = ('448,348', '708,518') if args.resize else ('300,60', '600,300')
    steps = ['0:wait=|Cover', '0:idle=800', f'0:{down}={start}', '0:idle=300',
             f'0:{move}={end}', '0:idle=300', f'0:{up}={end}',
             '0:idle=500', '0:windows=1', f'0:shot={shot}', '0:menu=quit']
    env = dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='cocoa' if args.native else 'dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE='30',
               NEO98_HOST_SCREEN='800x600', NEO98_CPU=args.engine,
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_MENU', 'NEO98_HOST_ATTACHED', 'NEO98_HOST_INPUT_ONCE'):
        env.pop(key, None)
    command = [str(ROOT / 'build/native-app/neo98-host'), '-', r'C:\WINDOWS\FONTS\VGAFIX.FON',
               '--quiet', '--system-tree', str(drive), '--exe', str(exe), '--drive-c', str(drive),
               '--no-registry-file', '--crash-report-dir', str(out / 'reports')]
    if args.mode != 'windowed':
        command.append('--seamless')
    if args.mode == 'layer':
        command.append('--seamless-layer')
    with (out / 'host.log').open('w') as log:
        process = subprocess.Popen(command, env=env, cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()  # Never SIGKILL a host.
            raise
        finally:
            if process.poll() is not None:
                shutil.rmtree(drive)
    log = (out / 'host.log').read_text(errors='replace')
    assert code == 0 and 'input wait=|Cover reached' in log
    magic, size, depth, pixels = shot.read_bytes().split(b'\n', 3)
    width, height = map(int, size.split())
    assert magic == b'P6' and (width, height) == (800, 600)
    def pixel(x, y):
        at = (y * width + x) * 3
        return pixels[at:at + 3].hex()
    checks = {
        'moved window painted the newly exposed area': pixel(380, 350) == 'ff0000',
        'moved window retained its other client pixels': pixel(650, 350) == 'ff0000',
        'cover window retained its client pixels': pixel(100, 150) == '0000ff',
    }
    if args.resize:
        checks = {
            'resized window painted the newly exposed area': pixel(100, 150) == 'ff0000',
            'resized window painted its new client area': pixel(650, 450) == 'ff0000',
            'corner drag produced 660 by 470': 'frame=50,50,710,520' in log,
        }
    (out / 'results.json').write_text(json.dumps(checks, indent=2) + '\n')
    for name, passed in checks.items():
        print(('PASS ' if passed else 'FAIL ') + name)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    sys.exit(main())
