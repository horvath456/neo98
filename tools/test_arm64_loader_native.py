#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The PE loader with a PE32+ image for ARM64 built by the SDK, and the
existing PE32 loader test, so the i386 path is seen to be unchanged.
docs/ARM64-APPS.md."""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import arm64_sdk  # noqa: E402

OUT = ROOT / 'build/arm64-loader-test'
FLAGS = ['-std=c++17', '-O1', '-g0', '-fsanitize=address,undefined']


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    OUT.mkdir(parents=True)
    # A program with absolute addresses in its data, so there are DIR64
    # relocations to apply.
    source = OUT / 'fixture.c'
    source.write_text('#include <windows.h>\n'
                      'static const char *const words[] = {"one", "two", "three"};\n'
                      'static const char *volatile current = "start";\n'
                      'int main(int argc, char **argv) {\n'
                      '    DWORD n; current = words[argc % 3];\n'
                      '    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), current, (DWORD)lstrlenA(current), &n, NULL);\n'
                      '    wsprintfA(argv[0], "%s", current); CloseHandle(NULL); return (int)GetTickCount(); }\n')
    program = arm64_sdk.build([source], OUT / 'fixture.exe')
    sources = [ROOT / 'src/pe32.cpp', ROOT / 'src/win98_exports.cpp']
    runner = OUT / 'test-arm64-loader'
    subprocess.run(['clang++', *FLAGS, str(ROOT / 'tools/test_arm64_loader.cpp'), *map(str, sources), '-o', str(runner)], check=True)
    result = subprocess.run([str(runner), str(program)], capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    if result.returncode or 'ARM64 LOADER PASS' not in result.stdout:
        return 1
    legacy = OUT / 'test-pe32'
    subprocess.run(['clang++', *FLAGS, str(ROOT / 'tools/test_pe32.cpp'), *map(str, sources), '-o', str(legacy)], check=True)
    result = subprocess.run([str(legacy)], capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    shutil.rmtree(OUT, ignore_errors=True)
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
