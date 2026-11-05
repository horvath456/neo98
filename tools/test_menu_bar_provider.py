#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise menu-bar callback context through native mouse and keyboard routing."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98

def test():
    out = ROOT / 'build/menu-tracking'
    out.mkdir(exist_ok=True)
    binary = out / 'test-menu-bar-provider'
    sources = ('user', 'user_draw', 'gdi', 'dib_text', 'dibeng', 'win98_rect', 'text_edit', 'win98_dialog')
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    str(ROOT / 'tools/test_menu_bar_provider.cpp'),
                    *[str(ROOT / 'src' / (name + '.cpp')) for name in sources],
                    '-o', str(binary)], env=neo98.tool_environment(), check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    (out / 'bar-provider.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
    report = {'status': 'PASS', 'sanitizers': ['address', 'undefined'],
              'runner_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'checks': ['menu-bar root handle and selected index', 'dynamic insertion during initialization',
                         'mouse opening and mnemonic selection', 'direct-popup callback context', 'cleanup']}
    (out / 'bar-provider-results.json').write_text(json.dumps(report, indent=2) + '\n')
    return report

if __name__ == '__main__':
    test()
