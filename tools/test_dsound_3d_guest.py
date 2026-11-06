#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""A 3D buffer of DirectSound over the driver and over the wave device.

Builds tools/dsound_3d_fixture.c and the runner tools/test_dsound_3d_guest.cpp
and runs the guest twice: over the native DSOUND.VXD and, with
NEO98_DSOUND_WAVE=1, over the wave device, where DSOUND.DLL mixes everything
itself. A mono buffer stands in front, to the left, to the right, far away
and comes closer; for every phase the level of each channel, the delay
between the channels and the pitch have to agree between the two runs.

    build/analysis-venv/bin/python tools/test_dsound_3d_guest.py \
        --objects build/native-app/objects --out build/dsound-3d --pristine

--pristine runs on the DirectX 6.1 of the setup folder (DSOUND.DLL 4.06)
instead of the DirectX 9 copy (4.09); --dx7, --dx8 and --dx81 run on a copy
with the DirectSound of DirectX 7.0, 8.0a or 8.1 (tools/dx_sound_tree.py).
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
parser.add_argument('--pristine', action='store_true')
parser.add_argument('--dx7', action='store_true')
parser.add_argument('--dx8', action='store_true')
parser.add_argument('--dx81', action='store_true')
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = (args.out or neo98.output_directory('dsound-3d')).resolve()
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


from dsound_components import IMPORTS  # noqa: E402

libs = []
for dll, entries in IMPORTS.items():
    definition = OUT / (dll + '.def')
    library = OUT / (dll + '.lib')
    definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                          ''.join(f'{n}@{b}\n' for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)

run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-c', RUN / 'tools/dsound_3d_fixture.c',
                '-o', OUT / 'fixture.obj'])
exe = OUT / 'D3D.EXE'
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
binary = OUT / 'test-dsound-3d'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_dsound_3d_guest.cpp', *objects, *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
import re


def measure(name, wave):
    env.pop('NEO98_DSOUND_WAVE', None)
    if wave:
        env['NEO98_DSOUND_WAVE'] = '1'
    text = run(name, [binary, exe, font])
    phases = {}
    for m in re.finditer(r'D3D (\w+?)_ phase=(\d) left=(\d+) right=(\d+) delay=(-?\d+) hertz=([\d.]+)', text):
        phases[(m.group(1), int(m.group(2)))] = (int(m.group(3)), int(m.group(4)), int(m.group(5)), float(m.group(6)))
    return phases


driver, reference = measure('guest-driver', False), measure('guest-wave', True)
good = bool(reference) and set(driver) == set(reference)
for key in sorted(reference):
    want, got = reference[key], driver.get(key)
    line = f'{key[0]} phase {key[1]}: wave device left={want[0]} right={want[1]} delay={want[2]} hertz={want[3]}'
    if got is None:
        print(line + ', driver: missing')
        continue
    line += f', driver left={got[0]} right={got[1]} delay={got[2]} hertz={got[3]}'
    # Levels within a tenth (and a floor of 40 for a channel that is nearly
    # silent), the delay within two frames, the pitch within two hertz.
    fine = all(abs(g - w) <= max(40, w / 10) for g, w in zip(got[:2], want[:2]))
    fine = fine and abs(got[2] - want[2]) <= 2 and abs(got[3] - want[3]) <= 3
    print(line + ('' if fine else '  DIFFERENT'))
    good = good and fine
if args.measure:
    print('DirectSound 3D MEASURED')
    raise SystemExit(0)
result = 'DirectSound 3D ' + ('PASS' if good else 'FAIL') + f' phases={len(reference)}'
print(result)
if not good:
    raise SystemExit(1)
(OUT / 'results.json').write_text(json.dumps({
    'status': 'PASS', 'result': result, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
}, indent=2) + '\n')
