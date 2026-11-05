#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build/run USER/GDI limit regressions under ASan/UBSan, via serial.py."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--case', type=int, default=6103)
    parser.add_argument('--source', type=Path, default=ROOT / 'src/user.cpp')
    parser.add_argument('--gdi-source', type=Path, default=ROOT / 'src/gdi.cpp')
    parser.add_argument('--label', default='after')
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    out = ROOT / 'build/user-gdi-limits'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / ('limits-' + args.label)
    sources = ['gdi', 'dib_text', 'dibeng', 'win98_rect', 'user_draw',
               'text_edit', 'win98_dialog', 'win98_parameters']
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-I' + str(ROOT / 'src'),
               '-DUSER_LIMITS_SOURCE="' + str(args.source.resolve()) + '"',
               str(ROOT / 'tools/test_user_gdi_limits.cpp'),
               *[str(args.gdi_source if name == 'gdi' else ROOT / 'src' / (name + '.cpp')) for name in sources], '-o', str(binary)]
    stages = [] if args.no_build else [('build', command)]
    stages.append(('run', [str(binary), str(args.case)]))
    try:
        for stage, cmd in stages:
            result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
            (out / f'{args.case}-{args.label}-{stage}.log').write_text(result.stdout + result.stderr)
            if result.returncode or stage == 'run':
                print(result.stdout + result.stderr, end='', flush=True)
            result.check_returncode()
    finally:
        shutil.rmtree(str(binary) + '.dSYM', ignore_errors=True)

if __name__ == '__main__':
    main()
