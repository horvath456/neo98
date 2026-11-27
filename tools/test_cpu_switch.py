#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Guests that take turns on the native processor: what each one sees, and what
a change from one to the next costs in system calls.

    python3 tools/test_cpu_switch.py

Builds tools/test_cpu_switch.cpp and runs it with both native modes: ptrace,
where every guest has a process of its own, and direct, where they share the
32-bit addresses and the LDT of the host process and the test bounds the
mmap and modify_ldt calls of a change (Cpu::SwitchCosts). The direct mode runs
a second time with NEO98_DIRECT_EXACT_LDT=1, which empties the LDT for a flat
guest too. Linux x86_64 only; elsewhere the test is skipped (exit 77)."""
from pathlib import Path
import os, platform, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tools/test_cpu_switch.cpp', 'src/x86_cpu.cpp', 'src/x86_address_space.cpp',
           'src/jit_memory.cpp', 'tools/host_native_cpu.cpp', 'tools/direct_bridge.S']


def main():
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64'):
        print('SKIP: the native CPU runs on Linux x86_64 only')
        return 77
    sys.path.insert(0, str(ROOT))
    import neo98
    environment = neo98.tool_environment()
    out = ROOT / 'build/cpu-switch'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'cpu-switch'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-DNEO98_NO_UNICORN',
                    *(str(ROOT / source) for source in SOURCES), '-ldl', '-pthread', '-o', str(binary)],
                   cwd=ROOT, env=environment, check=True)
    failed = False
    for mode, exact in (('ptrace', False), ('direct', False), ('direct', True)):
        env = {**os.environ, 'NEO98_NATIVE_MODE': mode}
        env.pop('NEO98_DIRECT_EXACT_LDT', None)
        if exact:
            env['NEO98_DIRECT_EXACT_LDT'] = '1'
        result = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=600)
        print(f"{mode}{', the LDT emptied for flat guests' if exact else ''}:\n"
              f"{(result.stdout + result.stderr).rstrip()}")
        if result.returncode or 'CPU SWITCH PASS' not in result.stdout:
            failed = True
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
