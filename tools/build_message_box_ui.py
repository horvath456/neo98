#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build only the small x86 MessageBox UI guest; never compile the native runtime."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]
IMPORTS = {
    'KERNEL32': {'ExitProcess': 4, 'GetModuleHandleA': 4},
    'USER32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16,
               'DestroyWindow': 4, 'SetFocus': 4, 'GetFocus': 0, 'IsWindow': 4,
               'IsWindowEnabled': 4, 'GetParent': 4, 'SetWindowTextA': 8,
               'MessageBoxA': 16, 'MessageBoxExA': 20, 'MessageBoxIndirectA': 4},
}

def build(project_root, source_root, out):
    sys.path.insert(0, str(project_root))
    import neo98
    env = neo98.tool_environment()
    out.mkdir(parents=True, exist_ok=True)
    def run(args):
        subprocess.run(list(map(str, args)), env=env, check=True)
    libraries = []
    for dll, entries in IMPORTS.items():
        definition, library = out / (dll + '.def'), out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
        libraries.append(library)
    source = source_root / 'tools/message_box_ui_fixture.c'
    obj, exe = out / 'fixture.obj', out / 'MBUI.EXE'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
         '-fno-builtin', '-fno-stack-protector', '-c', source, '-o', obj])
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
         '/nodefaultlib', '/machine:x86', '/out:' + str(exe), obj, *libraries])
    report = {'status': 'BUILT; native execution pending shared integration',
              'guest_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'dialogs': 5, 'error_range': [23100, 23125]}
    (out / 'guest-build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root', type=Path, default=ROOT)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/message-box-ui')
    args = parser.parse_args()
    build(args.project_root.resolve(), args.source_root.resolve(), args.out.resolve())
