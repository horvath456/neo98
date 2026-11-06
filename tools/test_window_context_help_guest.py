#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and run the focused context-help window contract."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

from window_context_help_components import ROOT, OUT, IMPORTS
sys.path.insert(0, str(ROOT))
import neo98


def _native_app():
    """tools/native_app.py beside this test: the engine of the runner per
    platform (runtime_names, engine_flags, unicorn_link)."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return _native_app


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    objects = OUT / 'objects'
    objects.mkdir(exist_ok=True)

    def run(command):
        subprocess.run(command, cwd=ROOT, env=env, check=True)

    libraries = []
    for dll, entries in IMPORTS.items():
        definition = OUT / (dll + '.def')
        library = OUT / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{name}@{size}\n' for name, size in entries.items()))
        run(['llvm-dlltool', '-m', 'i386', '-k', '-d', definition, '-l', library])
        libraries.append(library)

    fixture_object = OUT / 'fixture.obj'
    run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding',
         '-fno-builtin', '-fno-stack-protector', '-c',
         ROOT / 'tools/window_context_help_fixture.c', '-o', fixture_object])
    executable = OUT / 'CONTEXT.EXE'
    run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
         '/nodefaultlib', '/machine:x86', '/out:' + str(executable), fixture_object,
         *libraries])

    from native_app import engine_link_extra, runtime_objects
    runtime = runtime_objects()
    python = neo98.analysis_python()
    package = Path(subprocess.check_output(
        [str(python), '-c', 'import unicorn; print(unicorn.__file__)'],
        text=True).strip()).parent
    unicorn = package / 'lib' / 'libunicorn.2.dylib'
    runner = OUT / 'test-window-context-help'
    run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', *_native_app().engine_flags(),
         str(ROOT / 'tools/test_window_context_help_guest.cpp'), *map(str, runtime),
         *map(str, engine_link_extra()), *_native_app().unicorn_link(unicorn), '-o', runner])
    result = subprocess.run([runner, executable, ROOT / 'build/generated/notepad-fixedsys.fnt'],
                            cwd=ROOT, env=env, capture_output=True, text=True, check=True)
    print(result.stdout, end='')
    report = {'status': 'PASS', 'result': result.stdout.strip(),
              'guest_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'runner_sha256': hashlib.sha256(runner.read_bytes()).hexdigest(),
              'sanitizers': ['address', 'undefined'],
              'checks': ['zero default and explicit DWORD IDs', 'direct per-window IDs without parent inheritance',
                         'destroyed HWND rejection and ID reset on handle reuse'],
              'scope': 'Native window storage and public adapter behavior; USER32 wrapper ABI is checked separately.'}
    (OUT / 'guest-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
