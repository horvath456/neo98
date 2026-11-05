#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The unwinder of ARM64 programs against the code it describes.

Builds programs with the SDK (C with __try, C++ with the library, and, when
tools/games_arm64_logic.py has left them, the game logic of the ports) and
runs tools/test_arm64_unwind_host.cpp over them on the host, without a
machine: for every function the first instructions run in a model of the
processor, and after each one src/arm64_unwind.cpp must lead back to the
state of the caller.

    build/analysis-venv/bin/python tools/test_arm64_unwind_host.py

docs/ARM64-APPS.md, "Exceptions".
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import arm64_sdk  # noqa: E402

OUT = ROOT / 'build/arm64-unwind-test'


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    tool = OUT / 'test-arm64-unwind'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-Wall', str(ROOT / 'tools/test_arm64_unwind_host.cpp'),
                    str(ROOT / 'src/arm64_unwind.cpp'), '-o', str(tool)], check=True)
    programs = [arm64_sdk.build([ROOT / 'tools/arm64_seh_fixture.c'], OUT / 'seh.exe'),
                arm64_sdk.build([ROOT / 'tools/arm64_cxx_runtime_fixture.cpp'], OUT / 'cxx.exe'),
                arm64_sdk.build([ROOT / 'sdk/samples/hello.c'], OUT / 'hello.exe')]
    games = sorted((ROOT / 'build/arm64-cxx-scratch/games').glob('*/LOGIC.EXE'))
    result = subprocess.run([str(tool), *map(str, programs), *map(str, games)], capture_output=True, text=True)
    print(result.stdout.strip()[-3000:])
    summary = [line for line in result.stdout.splitlines() if line.startswith('UNWIND ')]
    if result.returncode or not summary or not summary[0].endswith('failures=0'):
        print('FAIL the unwinder leads every function back to its caller')
        return 1
    functions = int(summary[0].split('functions=')[1].split()[0])
    if functions < 1500:
        print(f'FAIL at least 1500 functions were walked ({functions})')
        return 1
    shutil.rmtree(OUT, ignore_errors=True)
    print(f'ARM64 UNWIND PASS programs={len(programs)} games={len(games)} {summary[0][7:]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
