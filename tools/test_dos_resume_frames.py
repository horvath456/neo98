#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native DOS continuation validation, including a damaged later frame.

Compiles the actual frame reader with upstream DOSBox archive readers and
writers under ASan/UBSan. No drive image or running game is needed.

    build/analysis-venv/bin/python tools/test_dos_resume_frames.py
"""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98


def main():
    out = neo98.output_directory('dos-resume-frames')
    binary = out / 'test-dos-resume-frames'
    subprocess.run(['/usr/bin/clang++', '-std=gnu++11', '-O1', '-g', '-fsanitize=address,undefined',
                    '-I', str(ROOT / 'tools'), '-I', str(ROOT / 'third_party/dosbox-pure/include'),
                    str(ROOT / 'tools/test_dos_resume_frames.cpp'),
                    str(ROOT / 'third_party/dosbox-pure/src/dbp_serialize.cpp'),
                    '-Wl,-dead_strip', '-o', str(binary)], check=True)
    return subprocess.run([str(binary)], check=False,
                          env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1',
                                   UBSAN_OPTIONS='halt_on_error=1')).returncode


if __name__ == '__main__':
    sys.exit(main())
