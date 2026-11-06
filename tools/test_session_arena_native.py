#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Two processes over the session arena, under ASan/UBSan.

The guest is one EXE that starts itself as its own child and a DLL whose
`.shrd` section carries IMAGE_SCN_MEM_SHARED. Both processes read and write
that section, allocate on one heap from HeapCreate with HEAP_SHARED and follow
pointers the other side stored there.

    build/analysis-venv/bin/python tools/test_session_arena_native.py
"""
from pathlib import Path
import argparse, ast, hashlib, json, shutil, subprocess, sys

SOURCE = Path(__file__).resolve().parents[1]
ROOT = SOURCE
OUT = ROOT / 'build/session-arena'
IMPORTS = {'KERNEL32': {'LoadLibraryA': 4, 'GetProcAddress': 8, 'FreeLibrary': 4, 'HeapCreate': 12,
                        'HeapAlloc': 12, 'HeapFree': 12, 'HeapSize': 12, 'HeapDestroy': 4,
                        'GetProcessHeap': 0, 'CreateProcessA': 40, 'WaitForSingleObject': 8,
                        'GetExitCodeProcess': 8, 'CloseHandle': 4, 'GetLastError': 0, 'SetLastError': 4,
                        'ExitProcess': 4, 'GetCommandLineA': 0, 'GetCurrentProcessId': 0,
                        'DisableThreadLibraryCalls': 4}}
# The base the DLL asks for. It has to be free in every process of the session,
# otherwise the loader moves the image and the section stays private.
DLL_BASE = '0x62000000'
COLLISION_BASE = '0xbfee0000'


def import_libraries(out, env):
    dlltool = shutil.which('llvm-dlltool', path=env['PATH'])
    assert dlltool, 'llvm-dlltool is missing'
    libs = []
    for dll, names in IMPORTS.items():
        definition = out / (dll + '.def')
        library = out / (dll + '.lib')
        definition.write_text('LIBRARY ' + dll + '.DLL\nEXPORTS\n' +
                              ''.join(f'{n}@{b}\n' for n, b in names.items()))
        subprocess.run([dlltool, '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)],
                       env=env, check=True)
        libs.append(str(library))
    return libs


def guest_binaries(source_root, env):
    """The EXE and the DLL, with the shared section flag really set."""
    import pefile
    libs = import_libraries(OUT, env)
    folder = source_root / 'tools'
    dll_obj, dll = OUT / 'shared.obj', OUT / 'SHRD.DLL'
    exe_obj, exe = OUT / 'fixture.obj', OUT / 'ARENA.EXE'
    compile_flags = ['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                     '-fno-stack-protector', '-c']
    subprocess.run([*compile_flags, str(folder / 'session_arena_shared.c'), '-o', str(dll_obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/dll', '/entry:DllMain', '/subsystem:windows', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/base:' + DLL_BASE, '/section:.shrd,RWS',
                    '/out:' + str(dll), str(dll_obj), *libs], env=env, check=True)
    subprocess.run([*compile_flags, str(folder / 'session_arena_fixture.c'), '-o', str(exe_obj)],
                   env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0',
                    '/nodefaultlib', '/machine:x86', '/out:' + str(exe), str(exe_obj), *libs],
                   env=env, check=True)
    # The test is only a test if the section really carries the flag and the
    # DLL really asks for the base the fixture counts on.
    image = pefile.PE(str(dll), fast_load=True)
    section = next((s for s in image.sections if s.Name.rstrip(b'\0') == b'.shrd'), None)
    assert section is not None, 'the guest DLL has no .shrd section'
    assert section.Characteristics & 0x10000000, \
        f'.shrd is not IMAGE_SCN_MEM_SHARED ({section.Characteristics:08x})'
    assert image.OPTIONAL_HEADER.ImageBase == int(DLL_BASE, 16), 'the guest DLL moved its base'
    ordinary = next((s for s in image.sections if s.Name.rstrip(b'\0') in (b'.data', b'.bss')), None)
    assert ordinary is None or not (ordinary.Characteristics & 0x10000000), \
        'the ordinary data section carries the shared flag'
    collision_source = folder / 'session_arena_collision.c'
    foreign_obj, foreign = OUT / 'foreign.obj', OUT / 'FOREIGN.DLL'
    collision_obj, collision = OUT / 'collision.obj', OUT / 'COLLIDE.DLL'
    subprocess.run([*compile_flags, '-DCOLLISION_VALUE=0x13579bdfu', str(collision_source),
                    '-o', str(foreign_obj)], env=env, check=True)
    subprocess.run([*compile_flags, '-DCOLLISION_VALUE=0x2468ace0u', str(collision_source),
                    '-o', str(collision_obj)], env=env, check=True)
    common_link = ['/dll', '/entry:DllMain', '/subsystem:windows', '/timestamp:0', '/nodefaultlib',
                   '/machine:x86', '/base:' + COLLISION_BASE]
    subprocess.run(['lld-link', *common_link, '/out:' + str(foreign), str(foreign_obj), *libs],
                   env=env, check=True)
    subprocess.run(['lld-link', *common_link, '/section:.text,ERS', '/out:' + str(collision),
                    str(collision_obj), *libs], env=env, check=True)
    foreign_image = pefile.PE(str(foreign), fast_load=False)
    collision_image = pefile.PE(str(collision), fast_load=False)
    foreign_text = next(s for s in foreign_image.sections if s.Name.rstrip(b'\0') == b'.text')
    collision_text = next(s for s in collision_image.sections if s.Name.rstrip(b'\0') == b'.text')
    assert not (foreign_text.Characteristics & 0x10000000), 'foreign .text is shared'
    assert collision_text.Characteristics & 0x10000000, 'collision .text is not shared'
    assert foreign_image.OPTIONAL_HEADER.ImageBase == collision_image.OPTIONAL_HEADER.ImageBase == int(COLLISION_BASE, 16)
    def export_rva(image):
        return next(s.address for s in image.DIRECTORY_ENTRY_EXPORT.symbols if s.name == b'CollisionBytes')
    assert export_rva(foreign_image) == export_rva(collision_image), 'collision function RVAs differ'
    return exe, dll, section, foreign, collision, collision_text


def test(source_root=SOURCE):
    sys.path.insert(0, str(ROOT))
    import neo98
    source_root = Path(source_root).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    exe, dll, section, foreign, collision, collision_text = guest_binaries(source_root, env)
    tree = ast.parse((source_root / 'tools/native_app.py').read_text())
    sources = next(ast.literal_eval(n.value) for n in ast.walk(tree)
                   if isinstance(n, ast.Assign)
                   and any(isinstance(t, ast.Name) and t.id == 'sources' for t in n.targets))
    package = Path(subprocess.check_output(
        [str(neo98.analysis_python()), '-c', 'import unicorn;print(unicorn.__file__)'], text=True).strip()).parent
    library = package / 'lib' / ('libunicorn.2.dylib' if sys.platform == 'darwin' else 'libunicorn.so.2')
    binary = OUT / 'test-session-arena-native'
    # The second engine when NEO98_CPU=fex asks for it.
    sys.path.insert(0, str(source_root / 'tools'))
    import native_app
    fex_extra = native_app.engine_link_extra()
    folder = source_root / 'tools'
    command = ['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
               str(folder / 'test_session_arena_native.cpp'), str(source_root / ('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),
               *[str(source_root / 'src' / f'{s}.cpp') for s in sources], *map(str, fex_extra), str(library),
               '-Wl,-rpath,' + str(library.parent), '-o', str(binary)]
    build = subprocess.run(command, env=env, capture_output=True, text=True)
    (OUT / 'build.log').write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(binary), str(exe), str(dll), str(foreign), str(collision),
                          str(ROOT / 'build/generated/notepad-fixedsys.fnt')],
                         env=env, capture_output=True, text=True)
    (OUT / 'native.log').write_text(run.stdout + run.stderr)
    print(run.stdout[-4000:] + run.stderr, end='')
    run.check_returncode()
    report = {'status': 'PASS', 'sanitizers': ['address', 'undefined'],
              'guest_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'dll_sha256': hashlib.sha256(dll.read_bytes()).hexdigest(),
              'shared_section': {'name': '.shrd',
                                 'characteristics': f'{section.Characteristics:08x}',
                                 'address': f'{int(DLL_BASE, 16) + section.VirtualAddress:08x}'},
              'overlap_section': {'name': '.text',
                                  'characteristics': f'{collision_text.Characteristics:08x}',
                                  'address': f'{int(COLLISION_BASE, 16) + collision_text.VirtualAddress:08x}'},
              'checks': [
                  'a section with IMAGE_SCN_MEM_SHARED has the same address in both processes',
                  'the bytes of that section are one set of bytes for the session',
                  'an ordinary data section of the same image stays private per process',
                  'a heap from HeapCreate with HEAP_SHARED lies at or above 80000000h',
                  'the second process allocates and frees on that very heap through its handle',
                  'a pointer into the shared heap means the same thing in both processes',
                  'a private heap of one process is ERROR_INVALID_PARAMETER in the other',
                  'HeapDestroy on the shared heap refuses the handle afterwards',
                  'a child shared section does not replace an unrelated parent image at the same address']}
    (OUT / 'results.json').write_text(json.dumps(report, indent=1) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source-root', type=Path, default=SOURCE)
    test(parser.parse_args().source_root)
