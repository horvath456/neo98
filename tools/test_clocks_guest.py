#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The clocks and the waits of the port against the host wall clock.

Builds tools/clocks_fixture.c, links tools/test_clocks_guest.cpp against the
sanitized runtime objects and runs it. The guest measures Sleep, SetTimer,
timeSetEvent, GetTickCount, timeGetTime and QueryPerformanceCounter with its
own clocks; the runner takes the host time of every phase boundary and checks
that the two agree.

    build/analysis-venv/bin/python tools/test_clocks_guest.py


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


def _native_app():
    """tools/native_app.py beside this test: the engine of the runner per
    platform (runtime_names, engine_flags, unicorn_link)."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return _native_app


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
OUT = (args.out or neo98.output_directory('clocks')).resolve()
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
FLAGS = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', *_native_app().engine_flags()]

IMPORTS = {
    'kernel32': {'GetTickCount': 0, 'QueryPerformanceCounter': 4, 'QueryPerformanceFrequency': 4,
                 'Sleep': 4, 'VirtualAlloc': 16, 'ExitProcess': 4, 'GetModuleHandleA': 4},
    'winmm': {'timeGetTime': 0, 'timeSetEvent': 20, 'timeKillEvent': 4, 'timeBeginPeriod': 4,
              'timeEndPeriod': 4},
    'user32': {'RegisterClassA': 4, 'CreateWindowExA': 48, 'DefWindowProcA': 16, 'SetTimer': 16,
               'KillTimer': 8, 'PeekMessageA': 20, 'TranslateMessage': 4, 'DispatchMessageA': 4,
               'DestroyWindow': 4},
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
                '-fno-stack-protector', '-c', RUN / 'tools/clocks_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'CLOCKS.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:windows', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((RUN / 'tools/native_app.py').read_text())
names = next(_native_app().runtime_names(ast.literal_eval(n.value)) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                  for t in n.targets))
objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / ('fiber_x86_64.S.o' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S.o')]
library = ROOT / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib'
binary = OUT / 'test-clocks'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_clocks_guest.cpp', *objects, *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
result = run('guest', [binary, exe, font])
print(result)
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
