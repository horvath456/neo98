#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise the POSIX session DIB backing spans under sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory(prefix='session-dib-', dir=ROOT / 'build') as folder:
        binary = str(Path(folder) / 'test')
        subprocess.run(['clang++', '-std=c++17',
                        '-fsanitize=address,undefined',
                        str(ROOT / 'tools/test_session_dib_memory.cpp'),
                        '-o', binary], check=True)
        subprocess.run([binary], check=True)

if __name__ == '__main__':
    main()
