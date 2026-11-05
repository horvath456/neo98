#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Guest for the SystemParametersInfo write branches, maximize and HWND_BROADCAST."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
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
sys.path.insert(0, str(ROOT / 'tools'))
import neo98
from native_build import compile_objects

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path, default=P.parents[1])
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
RUN = args.source_root.resolve()
OUT = args.out.resolve()
OUT.mkdir(parents=True, exist_ok=True)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
env = neo98.tool_environment()
env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
FLAGS = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined']


def run(name, command):
    p = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
    (OUT / (name + '.log')).write_text(p.stdout + p.stderr)
    if p.returncode:
        print(p.stdout + p.stderr)
    p.check_returncode()
    return p.stdout.strip()


from spi_write_components import IMPORTS

libs = []
for dll, entries in IMPORTS.items():
    definition, library = OUT / (dll + '.def'), OUT / (dll + '.lib')
    definition.write_text('LIBRARY %s.DLL\nEXPORTS\n' % dll +
                          ''.join('%s@%d\n' % (n, b) for n, b in entries.items()))
    run(dll, ['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
    libs.append(library)
run('fixture', ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                '-fno-stack-protector', '-DSPI_WRITE_STANDALONE', '-c',
                RUN / 'tools/spi_write_fixture.c', '-o', OUT / 'fixture.obj'])
exe = OUT / 'SPIWRITE.EXE'
run('fixture-link', ['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                     '/nodefaultlib', '/machine:x86', '/out:' + str(exe), OUT / 'fixture.obj', *libs])

tree = ast.parse((ROOT / 'tools/native_app.py').read_text())
names = next(ast.literal_eval(n.value) for n in ast.walk(tree)
             if isinstance(n, ast.Assign) and any(getattr(t, 'id', '') == 'sources' for t in n.targets))
objects = compile_objects(RUN, [RUN / 'src/fiber_aarch64.S', *[RUN / 'src' / (s + '.cpp') for s in names]],
                          FLAGS, env, ROOT / 'build/native-app/object-cache')
package = Path(subprocess.check_output([str(neo98.analysis_python()), '-c',
                                        'import unicorn; print(unicorn.__file__)'], text=True).strip()).parent
library = package / 'lib' / ('libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2')
binary = OUT / 'test-spi-write'
run('guest-link', ['clang++', *FLAGS, RUN / 'tools/test_spi_write_guest.cpp', *objects, *_engine_link_extra(), library,
                   '-Wl,-rpath,' + str(library.parent), '-o', binary])
font = ROOT / 'build/generated/notepad-fixedsys.fnt'
result = run('guest', [binary, exe, font])
print(result)
(OUT / 'results.json').write_text(json.dumps(
    {'status': 'PASS', 'result': result, 'runner_sha256': sha(binary), 'guest_sha256': sha(exe),
     'font_sha256': sha(font),
     'checks': ['SPI_GETWORKAREA before and after SPI_SETWORKAREA',
                'SPIF_SENDCHANGE broadcasts WM_SETTINGCHANGE to the top-level window',
                'unchanged work area succeeds without a broadcast',
                'a work area outside the monitor is refused and restores the previous one',
                'a NULL rectangle resets the work area to the monitor rectangle',
                'SM_CYFULLSCREEN and SM_CYMAXIMIZED follow the work area',
                'WM_GETMINMAXINFO reaches the window procedure and the maximized frame uses the work area',
                'SPI_SETNONCLIENTMETRICS round trip and cbSize rejection',
                'SPI_SETICONTITLELOGFONT round trip with the original Desktop section name',
                'HWND_BROADCAST SendMessageA reaches the top-level window'],
     'runtime_sha256': {str(p.relative_to(RUN)): sha(p) for p in sorted((RUN / 'src').iterdir())
                        if p.name != 'win98_class_public.h' and p.suffix in ('.h', '.cpp', '.inc', '.S')}},
    indent=2) + '\n')
