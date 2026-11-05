#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""host data structures under ASan/UBSan. Run through serial.py."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
out = ROOT / 'build/explorer-performance-native'
out.mkdir(parents=True, exist_ok=True)
common = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
for name, sources in [('import_index', []), ('bitmap_provider_edges', ['dibeng','dib_text','win98_rect']), ('explorer_regions', ['gdi','dib_text','dibeng','win98_rect','user_draw','text_edit','win98_dialog','win98_parameters'])]:
    binary = out / name
    try:
        command = common + [str(ROOT / 'tools' / ('test_' + name + '.cpp'))] + [str(ROOT / 'src' / (source + '.cpp')) for source in sources] + ['-o',str(binary)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (out / (name + '-build.log')).write_text(result.stdout + result.stderr)
        if result.returncode: print(result.stdout + result.stderr)
        result.check_returncode()
        result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True,
                                env=dict(os.environ, UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, end='')
        result.check_returncode()
    finally:
        shutil.rmtree(str(binary) + '.dSYM', ignore_errors=True)
