#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the own x86 virtual-memory fixture for host execution."""
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import neo98 as build


def generate():
    out = ROOT / 'build/virtual-memory'
    out.mkdir(exist_ok=True)
    functions = {'VirtualAlloc':16, 'VirtualFree':12, 'VirtualProtect':16, 'VirtualQuery':12,
                 'SetLastError':4, 'GetLastError':0, 'ExitProcess':4, 'GetCommandLineA':0, 'lstrcpyA':8}
    definition = out / 'kernel32.def'
    definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n' + ''.join(f'{n}@{s}\n' for n,s in functions.items()))
    commands = [
        ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', out / 'kernel32.lib'],
        ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-stack-protector', '-DVIRTUAL_STANDALONE',
         '-c', ROOT / 'tools/virtual_fixture.c', '-o', out / 'virtual.obj'],
        ['lld-link', '/machine:x86', '/entry:mainCRTStartup', '/subsystem:console', '/nodefaultlib', '/fixed',
         '/safeseh:no', '/timestamp:0', out / 'virtual.obj', out / 'kernel32.lib', '/out:' + str(out / 'VMEM.EXE')]]
    for command in commands: subprocess.run(list(map(str, command)), env=build.tool_environment(), check=True)
    data = (out / 'VMEM.EXE').read_bytes()
    text = '// Own x86 memory/protection fixture.\nstatic const unsigned char VirtualContractProgram[] = {\n'
    text += '\n'.join(','.join(str(b) for b in data[i:i+24]) + ',' for i in range(0, len(data), 24)) + '\n};\n'
    header = ROOT / 'build/generated/virtual_contract.h'
    if not header.exists() or header.read_text() != text: header.write_text(text)
    return out / 'VMEM.EXE'


if __name__ == '__main__': generate()
