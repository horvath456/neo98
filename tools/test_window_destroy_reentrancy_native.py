#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run native USER destruction callbacks under ASan and UBSan."""
from pathlib import Path
import ast
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


from native_app import ROOT
import neo98


OUT = ROOT / 'build/window-destroy-reentrancy'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    tree = ast.parse((ROOT / 'tools/native_app.py').read_text())
    sources = next(_native_app().runtime_names(ast.literal_eval(node.value)) for node in ast.walk(tree)
                   if isinstance(node, ast.Assign) and
                   any(isinstance(target, ast.Name) and target.id == 'sources'
                       for target in node.targets))
    sources = [source for source in sources if source != 'user']
    package = Path(subprocess.check_output(
        [str(neo98.analysis_python()), '-c', 'import unicorn;print(unicorn.__file__)'],
        text=True).strip()).parent
    library = package / 'lib' / ('libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2')
    binary = OUT / 'test-window-destroy-reentrancy'
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', *_native_app().engine_flags(),
               str(ROOT / 'tools/test_window_destroy_reentrancy_native.cpp'),
               str(ROOT / ('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),
               *[str(ROOT / 'src' / f'{source}.cpp') for source in sources],
               *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', str(binary)]
    built = subprocess.run(command, env=env, capture_output=True, text=True)
    (OUT / 'build.log').write_text(built.stdout + built.stderr)
    built.check_returncode()
    run = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
    (OUT / 'run.log').write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
