#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A DirectSound streaming buffer from an x86 guest over the original DSOUND.DLL.

Builds tools/dsound_stream_fixture.c, links the runner
tools/test_dsound_stream_guest.cpp against the sanitized runtime objects and
runs it. The guest keeps a looping buffer of a second written a tenth of a
second ahead of the write cursor, as a game with a mixer of its own does; the
runner measures the steps of the play cursor, the distance of the write
cursor, underruns and the time from Unlock to the mixer.

    build/analysis-venv/bin/python tools/test_dsound_stream_guest.py \
        --objects build/native-app/objects --out build/dsound-stream

--measure reports the numbers without checking them; --ring3 makes the VxD
leave the mixer of DSOUND 4.06 to 4.08 in DDHELP; --pristine runs on the
DirectX 6.1 of the setup folder instead of the DirectX 9 copy; --dx7, --dx8
and --dx81 run on a copy with the DirectSound of DirectX 7.0, 8.0a or 8.1
(tools/dx_sound_tree.py).


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

# The DirectX 9 copy of the Win98 folder (tools/dx9_tree.py) carries DSOUND.DLL
# 4.09, the pristine folder of setup DSOUND.DLL 4.06 of DirectX 6.1. The run
# measures the first by default and the second with --pristine.
import os  # noqa: E402
if '--pristine' in sys.argv:
    import win98_c  # noqa: E402
    win98_c.use_pristine()
elif {'--dx7', '--dx8', '--dx81'} & set(sys.argv):
    import dx_sound_tree  # noqa: E402
    _OTHER = dx_sound_tree.select(sys.argv)[1]
    if _OTHER is None:
        print('SKIP: no DirectSound tree of that DirectX')
        raise SystemExit(77)
    os.environ['NEO98_WIN98_ROOT'] = str(_OTHER)
else:
    import dx9_tree  # noqa: E402
    _DX9 = dx9_tree.require_tree()
    if _DX9 is None:
        print('SKIP: no DirectX 9 tree')
        raise SystemExit(77)
    os.environ['NEO98_WIN98_ROOT'] = str(_DX9)

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=P.parents[1])
parser.add_argument('--out', type=Path, default=None)
parser.add_argument('--objects', type=Path, default=None)
parser.add_argument('--rebuild-affected', action='store_true')
parser.add_argument('--measure', action='store_true')
parser.add_argument('--ring3', action='store_true')
parser.add_argument('--pristine', action='store_true')
parser.add_argument('--dx7', action='store_true')
parser.add_argument('--dx8', action='store_true')
parser.add_argument('--dx81', action='store_true')
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = (args.out or neo98.output_directory('dsound-stream')).resolve()
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


from dsound_stream_components import IMPORTS  # noqa: E402

libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/dsound_stream_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'DSTREAM.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((RUN / 'tools/native_app.py').read_text())
names = next(_native_app().runtime_names(ast.literal_eval(n.value)) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'sources'
                                                  for t in n.targets))
objects = [OBJECTS / (n + '.cpp.o') for n in names] + [OBJECTS / ('fiber_x86_64.S.o' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S.o')]
for name in (['sound_mixer', 'x86_wave_out', 'x86_dsound_vxd'] if args.rebuild_affected else []):
    run(name, ['clang++', *FLAGS, '-c', RUN / 'src' / (name + '.cpp'), '-o', OBJECTS / (name + '.cpp.o')])

library = ROOT / 'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib'
binary = OUT / 'test-dsound-stream'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_dsound_stream_guest.cpp', *objects, *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
if args.ring3:
    env['NEO98_DSOUND_RING3'] = '1'
result = run('guest', [binary, exe, font, *(['measure'] if args.measure else ['ring3'] if args.ring3 else [])])
print(result)
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
