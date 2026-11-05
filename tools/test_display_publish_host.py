#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Win98DisplayModes::Publish (src/win98_display_modes.h): the screen surface
of a display mode onto the host image.

Builds tools/test_display_publish_host.cpp for the build machine and runs it:
every depth (8, 16, 24, 32 bits) between ten sizes, each result compared
byte for byte with the plain rule (nearest source column and row, Pixel),
and the time of one picture of 640x480x16 on the host image.

    build/analysis-venv/bin/python tools/test_display_publish_host.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/display-publish-test'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    program = OUT / 'publish'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'tools/test_display_publish_host.cpp'),
                    '-o', str(program)], check=True)
    result = subprocess.run([str(program)], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    if result.returncode or 'failures=0' not in result.stdout:
        print('DISPLAY PUBLISH FAIL')
        return 1
    program.unlink()
    print('DISPLAY PUBLISH PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
