#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Software interrupts from 32-bit guest code, under ASan/UBSan.

The guest assembles the four instructions of an NTDLL stub into an executable
block and runs them, once per service number the NTDLL of the tree uses, plus
one int 21h.
    build/analysis-venv/bin/python tools/test_interrupts32_native.py
"""
from pathlib import Path
import argparse, ast, hashlib, json, shutil, subprocess, sys

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


SOURCE = Path(__file__).resolve().parents[1]
OUT = SOURCE / 'build/interrupts32'
IMPORTS = {'KERNEL32': {'GetProcessHeap': 0, 'HeapAlloc': 12, 'HeapFree': 12, 'ExitProcess': 4}}


def test(source_root=SOURCE):
    sys.path.insert(0, str(SOURCE))
    import neo98
    source_root = Path(source_root).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool
    libs = []
    for dll, names in IMPORTS.items():
        definition = OUT / (dll + '.def')
        library = OUT / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{n}@{b}\n' for n, b in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libs.append(str(library))
    folder = source_root / 'tools'
    obj, exe = OUT / 'fixture.obj', OUT / 'INTR.EXE'
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                    '-fno-stack-protector', '-c', str(folder / 'interrupts32_fixture.c'), '-o', str(obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(obj), *libs],
                   env=env, check=True)
    tree = ast.parse((source_root / 'tools/native_app.py').read_text())
    sources = next(_native_app().runtime_names(ast.literal_eval(n.value)) for n in ast.walk(tree)
                   if isinstance(n, ast.Assign)
                   and any(isinstance(t, ast.Name) and t.id == 'sources' for t in n.targets))
    package = Path(subprocess.check_output(
        [str(neo98.analysis_python()), '-c', 'import unicorn;print(unicorn.__file__)'], text=True).strip()).parent
    library = package / 'lib' / ('libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2')
    binary = OUT / 'test-interrupts32-native'
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', *_native_app().engine_flags(),
               str(folder / 'test_interrupts32_native.cpp'), str(source_root / ('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),
               *[str(source_root / 'src' / f'{s}.cpp') for s in sources], *_engine_link_extra(), *_native_app().unicorn_link(library), '-o', str(binary)]
    build = subprocess.run(command, env=env, capture_output=True, text=True)
    (OUT / 'build.log').write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary), str(exe), str(SOURCE / 'build/generated/notepad-fixedsys.fnt')],
                         env=env, capture_output=True, text=True)
    (OUT / 'native.log').write_text(run.stdout + run.stderr)
    print(run.stdout[-3000:] + run.stderr, end='')
    run.check_returncode()
    # int 21h from a 32-bit process bound the missing Win16 machine.
    assert 'runtime error' not in run.stderr, run.stderr[-2000:]
    report = {'status': 'PASS', 'sanitizers': ['address', 'undefined'],
              'guest_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'services': ['107', '108', '109', '10b', '194', '195', '196'],
              'checks': [
                  'int 2Eh reaches the native gate instead of ending as a CPU exception',
                  'every service number of the NTDLL of the tree but 108h answers STATUS_NOT_IMPLEMENTED',
                  'NtPowerInformation SystemPowerCapabilities answers the NTKERN record',
                  'NtPowerInformation levels 0 to 3, 8 and 9 keep and check the NTKERN policies',
                  'int 21h from 32-bit code answers the DOS version 0A07h',
                  'int 21h from 32-bit code runs without a sanitizer report']}
    (OUT / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, default=SOURCE)
    test(parser.parse_args().source_root)
