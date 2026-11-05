#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""The additional imported exports, called from an x86 guest.

Builds tools/imports2_fixture.c, links tools/test_imports2_guest.cpp against the
sanitized runtime objects and runs it. Every check the fixture makes is counted;
a failing check names itself with its own number.

    build/analysis-venv/bin/python tools/test_imports2_guest.py
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
OUT = (args.out or neo98.output_directory('imports2')).resolve()
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
    'kernel32': {'ExitProcess': 4, 'VirtualAlloc': 16, 'GetModuleHandleA': 4, 'GetProcAddress': 8,
                 'CreateEventA': 16, 'WaitForSingleObject': 8, 'CloseHandle': 4, 'GetLastError': 0, 'SetLastError': 4},
    'user32': {'ScrollDC': 28, 'VkKeyScanA': 4, 'VkKeyScanExA': 8, 'GetKeyboardLayout': 4},
    'winmm': {'DriverCallback': 28, 'waveInGetErrorTextA': 12, 'midiInGetErrorTextA': 12},
    'gdi32': {'CreateCompatibleDC': 4, 'DeleteDC': 4, 'CreateDIBSection': 24,
              'SelectObject': 8, 'DeleteObject': 4, 'CreateRectRgn': 16,
              'SetRectRgn': 20, 'GetRgnBox': 8, 'CombineRgn': 16,
              'SelectClipRgn': 8, 'GetClipBox': 8, 'SetViewportOrgEx': 16,
              'EnumObjects': 16, 'AddFontResourceA': 4, 'RemoveFontResourceA': 4,
              'CreateScalableFontResourceA': 16, 'CreateColorSpaceA': 4,
              'DeleteColorSpace': 4, 'GetLogColorSpaceA': 12, 'SetColorSpace': 8,
              'GetColorSpace': 4, 'SetICMProfileA': 8},
}


def run(name, command, check=True):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode and check:
        print(p.stdout + p.stderr)
        p.check_returncode()
    return p.stdout


libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll.upper() + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/imports2_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'IMPORTS2.EXE'
run('fixture-link', ['lld-link', '/entry:main', '/subsystem:console', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((RUN / 'tools/native_app.py').read_text())
names = next(ast.literal_eval(n.value) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                  for t in n.targets))
objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / 'fiber_aarch64.S.o']
library = next((ROOT / 'build/analysis-venv/lib').glob('python3*/site-packages/unicorn/lib/libunicorn.2.dylib'))
binary = OUT / 'test-imports2'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_imports2_guest.cpp', *objects, *_engine_link_extra(), library,
                   '-Wl,-rpath,' + str(library.parent), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'

text = run('guest', [binary, exe, font])
head = dict(p.split('=', 1) for p in
            next(l for l in text.splitlines() if l.startswith('IMPORTS2 status=')).split()[1:])
counters = dict(p.split('=', 1) for p in
                next(l for l in text.splitlines() if l.startswith('IMPORTS2 checks=')).split()[1:])
error = next((l[len('IMPORTS2 error='):] for l in text.splitlines()
              if l.startswith('IMPORTS2 error=')), '')

problems = []
if head.get('status') != '2':
    problems.append(f"the run ended with status {head.get('status')}: {error}")
if head.get('code') != '0':
    problems.append(f"the run ended with code {head.get('code')}")
if head.get('read') != '1':
    problems.append('the counter block could not be read')
if int(counters.get('checks', 0)) < 8000:
    problems.append(f"only {counters.get('checks')} checks ran")
if int(counters.get('bad', 1)):
    problems.append(f"{counters['bad']} checks failed, the first one is number {counters['first']}")

if problems:
    print('\n'.join('FAIL ' + p for p in problems))
    raise SystemExit(1)
result = f"IMPORTS2 {counters['checks']} checks passed in {head['calls']} API calls"
print(result)
(OUT / 'guest-results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'checks': int(counters['checks']),
    'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
