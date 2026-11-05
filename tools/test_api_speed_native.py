#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check dispatch bookkeeping without requiring an installed Windows tree."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98
# Rule selection must not make Linux depend on a Mach-only variable.
sys.path.insert(0, str(ROOT / 'tools'))
import state_layout
from unittest.mock import patch
clock_variable = 'HostClock::Microseconds()::divisor@x86_threads.cpp'
with patch.object(sys, 'platform', 'linux'):
    assert clock_variable not in state_layout.read_rules()['var']
with patch.object(sys, 'platform', 'darwin'):
    assert state_layout.read_rules()['var'][clock_variable][0] == 'host'
out = ROOT / 'build/api-speed-test'
out.mkdir(parents=True, exist_ok=True)
env = neo98.tool_environment()
runner = out / 'test-api-speed'
subprocess.run(['clang++', '-std=c++17', '-O2', '-fsanitize=address,undefined',
                str(ROOT / 'tools/test_api_speed_native.cpp'), str(ROOT / 'src/crash_report.cpp'),
                '-o', str(runner)], env=env, check=True)
subprocess.run([str(runner)], env=env, check=True)
