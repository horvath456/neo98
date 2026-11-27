#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Selectors of blocks larger than 64 KiB take the place of freed ones.

A block of the 16-bit global heap that is larger than 64 KiB has one selector
for every 64 KiB, consecutive in the local descriptor table (__AHINCR). The
table handed such runs out from its end only, so every large block a program
allocated and freed used the table up a little more: a player that opened
one film after the other ran out of selectors.

    python3 tools/test_selector_runs.py

Builds tools/test_selector_runs.cpp against src/win98_selector_table.h and
runs it. Needs no Windows."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    sys.path.insert(0, str(ROOT))
    import neo98
    out = ROOT / 'build/selector-runs'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'selector-runs'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', str(ROOT / 'tools/test_selector_runs.cpp'), '-o',
                    str(binary)], cwd=ROOT, env=neo98.tool_environment(), check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
    print((result.stdout + result.stderr).rstrip())
    return 0 if not result.returncode and 'SELECTOR RUNS PASS' in result.stdout else 1


if __name__ == '__main__':
    sys.exit(main())
