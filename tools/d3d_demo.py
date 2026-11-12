#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run DEMO3D.EXE, the Direct3D 7 demo of tools/d3d_demo.c.

    build/analysis-venv/bin/python tools/d3d_demo.py # build, then open it in the host window
    build/analysis-venv/bin/python tools/d3d_demo.py --build # only build build/direct3d/DEMO3D.EXE
    build/analysis-venv/bin/python tools/d3d_demo.py --measure # headless run, prints the frame rate

Arguments after -- go to the demo: 16 or 32 for the colour depth, WIDTHxHEIGHT
for the mode, detail=0..2 for the torus, frames=N to stop after N frames,
novsync to flip with DDFLIP_NOVSYNC, hal to take the rasterizing device
instead of the transform-and-lighting one. The display keeps its 60 pictures
a second for every flip; NEO98_UNCAPPED=1 in the environment takes that clock
away for a measurement.

The measurement runs the optimised probe runner (no sanitizers), so its
numbers are the ones a person sees; it writes the last frame to
build/direct3d/demo.ppm and the log to build/direct3d/demo.log.


"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/direct3d'
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98  # noqa: E402

IMPORTS = {
    'KERNEL32': {'LoadLibraryA': 4, 'GetProcAddress': 8, 'OutputDebugStringA': 4, 'ExitProcess': 4,
                 'GetCommandLineA': 0},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'ShowWindow': 8,
               'SetForegroundWindow': 4, 'SetCursor': 4, 'PeekMessageA': 20, 'TranslateMessage': 4,
               'DispatchMessageA': 4, 'PostQuitMessage': 4, 'MessageBoxA': 16, 'AdjustWindowRect': 12,
               'GetClientRect': 8, 'ClientToScreen': 8},
    'GDI32': {'GetStockObject': 4, 'SelectObject': 8, 'SetTextColor': 8, 'SetBkColor': 8, 'SetBkMode': 8,
              'TextOutA': 20, 'GetTextMetricsA': 8},
    'WINMM': {'timeGetTime': 0, 'timeBeginPeriod': 4},
}
FONT = ROOT / 'build/generated/notepad-fixedsys.fnt'


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool, 'llvm-dlltool'
    libraries = []
    for dll, functions in IMPORTS.items():
        definition, library = OUT / f'demo-{dll}.def', OUT / f'demo-{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in functions.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(str(library))
    obj, exe = OUT / 'DEMO3D.obj', OUT / 'DEMO3D.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O2', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(ROOT / 'tools/d3d_demo.c'), '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', '/out:' + str(exe), str(obj), *libraries], env=env, check=True)
    return exe


def run_host(exe, demo_args):
    from native_app import build_host
    host = build_host()
    command = ' '.join(['DEMO3D.EXE', *demo_args])
    subprocess.run([str(host), str(exe), str(FONT), '--system-tree', '--command', command], check=False)


def measure(exe, demo_args, seconds, *, runner=None, system_tree=None):
    from native_app import build_fast
    runner = runner or build_fast('probe_application.cpp', 'probe-application-fast')
    drive = OUT / 'demo-c'
    shutil.rmtree(drive, ignore_errors=True)
    shot = OUT / 'demo.ppm'
    command = ' '.join(['DEMO3D.EXE', *demo_args])
    tree_flags = ['--system-tree', str(system_tree)] if system_tree else ['--system-tree']
    result = subprocess.run([str(runner), str(exe), str(FONT), *tree_flags, '--drive-c', str(drive),
                             '--registry', str(OUT / 'demo.bin'), '--command', command,
                             '--screenshot', str(shot), '--time-limit', str(seconds)],
                            env=neo98.tool_environment(), capture_output=True, text=True, timeout=seconds * 4 + 300)
    log = result.stdout + result.stderr
    (OUT / 'demo.log').write_text(log)
    lines = [line for line in log.splitlines() if 'D3DDEMO' in line]
    for line in lines:
        print(line[line.index('D3DDEMO'):])
    summary = next((line for line in log.splitlines() if line.startswith('PROBE started=')), '')
    print(summary)
    rates = [float(m) for m in re.findall(r'D3DDEMO fps=([0-9.]+)', log)]
    if not rates:
        raise SystemExit('the demo reported no frame rate; see build/direct3d/demo.log')
    # The first half second includes start-up; the rest is the steady rate.
    steady = rates[1:] or rates
    print(f'D3DDEMO steady fps: mean {sum(steady) / len(steady):.1f}, min {min(steady):.1f}, max {max(steady):.1f}'
          f' over {len(steady)} half-second windows; screenshot {shot}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--build', action='store_true', help='only build DEMO3D.EXE')
    parser.add_argument('--measure', action='store_true', help='run headless and print the frame rate')
    parser.add_argument('--seconds', type=int, default=20, help='time limit of the measurement run')
    parser.add_argument('--runner', type=Path, help='existing headless runner for --measure')
    parser.add_argument('--system-tree', type=Path, help='original Win98 files for --measure')
    parser.add_argument('demo_args', nargs='*', help='arguments for the demo, after --')
    args = parser.parse_args()
    exe = build()
    print(exe)
    if args.build:
        return
    if args.measure:
        measure(exe, args.demo_args, args.seconds, runner=args.runner, system_tree=args.system_tree)
    else:
        run_host(exe, args.demo_args)


if __name__ == '__main__':
    main()
