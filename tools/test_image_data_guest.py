#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Win32 VirtualProtect/VirtualQuery on an actual mixed-section PE."""
from pathlib import Path
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
import neo98
import arm64_run
import pefile
import win98_c
from native_app import build_host

out = ROOT / 'build/image-data-guest'
out.mkdir(exist_ok=True)
env = neo98.tool_environment()
imports = {'ExitProcess': 4, 'GetModuleHandleA': 4, 'GetCommandLineA': 0,
           'VirtualProtect': 16, 'VirtualQuery': 12, 'VirtualAlloc': 16}
definition = out / 'KERNEL32.def'
definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n' + ''.join(f'{k}@{v}\n' for k, v in imports.items()))
library = out / 'KERNEL32.lib'
subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
obj = out / 'image-data.obj'
subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', str(ROOT / 'tools/image_data_fixture.c'), '-o', str(obj)], env=env, check=True)
exe = out / 'IMAGEDAT.EXE'
subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0', '/nodefaultlib',
                '/opt:noref', '/machine:x86', '/safeseh:no', '/out:' + str(exe), str(obj), str(library)], env=env, check=True)
pe = pefile.PE(str(exe))
sections = {s.Name.rstrip(b'\0').decode(): {'rva': s.VirtualAddress, 'size': s.Misc_VirtualSize,
            'flags': s.Characteristics} for s in pe.sections}
assert sections['.data']['rva'] & 16383
assert not sections['.rdata']['flags'] & 0xa0000000
(out / 'layout.json').write_text(json.dumps(sections, indent=2) + '\n')
host = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else build_host()
baseline = '--baseline' in sys.argv
system_tree = win98_c.use_pristine()
for mode in range(8):
    command = [str(host), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
        '--exe', str(exe), '--command', str(mode), '--system-tree', str(system_tree),
        '--ram-drive', '--no-registry-file', '--quiet']
    result = arm64_run.run(command, timeout=60, cwd=ROOT,
        env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NEO98_HOST_DEADLINE='8',
                 NEO98_HOST_INPUT='1:key=0d;2:key=0d',
                 NEO98_FEX_DEBUG='1', NEO98_LOG_DIR=str(out / 'logs'), NEO98_SETTINGS_DIR=str(out / 'settings')))
    text = result.stdout + result.stderr
    (out / f'mode-{mode}-{os.environ.get("NEO98_CPU", "fex")}{"-before" if baseline else ""}.log').write_text(text)
    if mode == 0 and not baseline:
        okay = 'NEO98 HOST exited code=0' in text
    elif mode >= 6:
        okay = 'Guest write violates page protection' in text
    else:
        okay = 'EXCEPTION c0000005' in text and 'info=2,00000001,' in text
    okay = okay and result.returncode == 0 and not result.timed_out
    print(f'guest mode={mode} {"PASS" if okay else "FAIL"}', flush=True)
    if not okay:
        print(text[-5000:])
        raise SystemExit(1)
