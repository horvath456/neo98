#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""DirectSoundCapture of the original DSOUND.DLL on the test microphone.

Builds tools/dsound_capture_fixture.c, which loads DSOUND.DLL of the tree by
hand and records through IDirectSoundCapture in 8-bit mono at 11.025 Hz and
16-bit stereo at 44.100 Hz, links the runner
tools/test_dsound_capture_guest.cpp against the sanitized runtime objects and
runs it once per kind of the test microphone (NEO98_TEST_MICROPHONE): sine,
denied and none. The real microphone is never opened, and nothing plays.

    build/analysis-venv/bin/python tools/test_dsound_capture_guest.py \
        --objects build/native-app/objects --out build/dsound-capture


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
parser.add_argument('--rebuild-affected', action='store_true')
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = (args.out or neo98.output_directory('dsound-capture')).resolve()
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


def run(name, command):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode:
        print(p.stdout + p.stderr)
    p.check_returncode()
    return p.stdout.strip()


from dsound_capture_components import IMPORTS  # noqa: E402

libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/dsound_capture_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'DSCAPT.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((RUN / 'tools/native_app.py').read_text())
names = next(_native_app().runtime_names(ast.literal_eval(n.value)) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                  for t in n.targets))
objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / ('fiber_x86_64.S.o' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S.o')]
for name in (['sound_microphone', 'x86_dsound_vxd', 'x86_mm_timer'] if args.rebuild_affected else []):
    run(name, ['clang++', *FLAGS, '-c', RUN / 'src' / (name + '.cpp'), '-o', OBJECTS / (name + '.cpp.o')])

library = ROOT / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib'
binary = OUT / 'test-dsound-capture'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_dsound_capture_guest.cpp', *objects, *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
import pefile
import win98_c
win98_c.use_pristine()
sound_dll = win98_c.path(r'C:\WINDOWS\SYSTEM\DSOUND.DLL')
info = pefile.PE(str(sound_dll)).VS_FIXEDFILEINFO[0]
version = (info.FileVersionMS >> 16, info.FileVersionMS & 65535,
           info.FileVersionLS >> 16, info.FileVersionLS & 65535)
# The 6.1 DLL translates MMSYSERR_NODRIVER twice in its preferred-device
# query, producing E_FAIL. The 9.0c DLL returns NODRIVER.
# Both are original DLL results; never accept either result indiscriminately.
no_device_results = {(4, 6, 3, 518): 0x80004005, (4, 9, 0, 904): 0x88780078}
assert version in no_device_results, f'Unqualified DSOUND.DLL version: {version}'
results = {}
for kind in ('sine', 'denied', 'none'):
    env['NEO98_TEST_MICROPHONE'] = kind
    results[kind] = run('guest-' + kind, [binary, exe, font, kind, hex(no_device_results[version])])
    print(results[kind])
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'results': results, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
    'dsound_version': version, 'dsound_sha256': sha(sound_dll),
}, indent=2) + '\n')
