#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""USER and GDI state that depends on the installed Win98 tree.

Builds tools/user_system_state_fixture.c, links
tools/test_user_system_state_guest.cpp against the sanitized runtime objects
and runs it with the tree mounted. The fixture exits with the code of the
first failed check.

    build/analysis-venv/bin/python tools/test_user_system_state_guest.py

"""
from pathlib import Path
import argparse
import ast
import hashlib
import json
import subprocess
import sys

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]


P = Path(__file__).resolve()
ROOT = next(p for p in P.parents if (p / 'neo98.py').exists() and (p / 'build/analysis-venv').exists())
sys.path.insert(0, str(ROOT))
import neo98  # noqa: E402

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=P.parents[1])
parser.add_argument('--out', type=Path, default=None)
parser.add_argument('--objects', type=Path, default=None)
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = (args.out or neo98.output_directory('user-system-state')).resolve()
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(RUN / 'tools'))
if args.objects:
    OBJECTS = args.objects.resolve()
else:
    import native_app
    OBJECTS = Path(native_app.object_directory())

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
env = neo98.tool_environment()
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
FLAGS = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined']

IMPORTS = {
    'kernel32': {'ExitProcess': 4, 'SetLastError': 4, 'GetLastError': 0, 'GetModuleHandleA': 4,
                 'Sleep': 4, 'LoadLibraryA': 4},
    'user32': {'SystemParametersInfoA': 16, 'SwapMouseButton': 4, 'GetSystemMetrics': 4,
               'GetAsyncKeyState': 4, 'GetKeyState': 4, 'mouse_event': 20, 'RegisterClassA': 4,
               'CreateWindowExA': 48, 'DefWindowProcA': 16, 'PeekMessageA': 20,
               'DispatchMessageA': 4, 'DestroyWindow': 4, 'LoadImageA': 24, 'IsZoomed': 4},
    'gdi32': {'GetObjectA': 12, 'CreateCompatibleDC': 4, 'SelectObject': 8, 'GetPixel': 12,
              'DeleteDC': 4, 'DeleteObject': 4},
}


def run(name, command):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode:
        print(p.stdout + p.stderr)
    p.check_returncode()
    return p.stdout.strip()


libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll.upper() + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/user_system_state_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'STATE.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((RUN / 'tools/native_app.py').read_text())
names = next(ast.literal_eval(n.value) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                  for t in n.targets))
objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / 'fiber_aarch64.S.o']
library = ROOT / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib'
binary = OUT / 'test-user-system-state'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_user_system_state_guest.cpp', *objects, *_engine_link_extra(), library,
                   '-Wl,-rpath,' + str(library.parent), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
result = run('guest', [binary, exe, font])
print(result)
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
