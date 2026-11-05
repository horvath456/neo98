#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The existing file and registry guests through the host, with DOS HFILEs.

Runs the contracts of file_fixture.c and registry_fixture.c on a private
clone of setup's C:. The native file test separately covers HFILE lifetime
and inheritance under ASan/UBSan. No window or sound.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import neo98
import win98_c
from native_app import ROOT, build_host
from test_device_manager_host import host, failures


def main():
    out = neo98.output_directory('legacy-files-host')
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    names = {}
    objects = []
    checks = 0
    for source in ('file_fixture.c', 'registry_fixture.c'):
        text = (ROOT / 'tools' / source).read_text()
        checks += len(re.findall(r'\bcheck\(', text)) - 1
        for name, arguments in re.findall(r'^API\s+.*?\b(\w+)\(([^;]*)\);', text, re.M):
            names[name] = 4 * (arguments.count(',') + 1) if arguments.strip() != 'void' else 0
        obj = out / f'{source}.obj'
        subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-fno-builtin',
                        '-fno-stack-protector', '-c', str(ROOT / 'tools' / source), '-o', str(obj)], env=env, check=True)
        objects.append(obj)
    libraries = []
    names.update(FormatMessageA=28, LocalFree=4)
    for dll in ('KERNEL32', 'USER32', 'ADVAPI32', 'SHLWAPI'):
        def owner(name):
            return 'ADVAPI32' if name.startswith('Reg') else 'SHLWAPI' if name.startswith('Path') else \
                   'USER32' if name == 'ShowCursor' else 'KERNEL32'
        definition, library = out / f'{dll}.def', out / f'{dll}.lib'
        definition.write_text(f'LIBRARY {dll}.DLL\nEXPORTS\n' + ''.join(
            f'{name}@{size}\n' for name, size in names.items() if owner(name) == dll))
        subprocess.run(['llvm-dlltool', '-m', 'i386', '-k', '-d', str(definition), '-l', str(library)], env=env, check=True)
        libraries.append(library)
    source, obj, exe = out / 'main.c', out / 'main.obj', out / 'LEGACY.EXE'
    source.write_text('typedef unsigned U;\n'
                      '__declspec(dllimport) void __stdcall ExitProcess(U);\n'
                      '__declspec(dllimport) U __stdcall FormatMessageA(U,const char*,U,U,void*,U,void*);\n'
                      '__declspec(dllimport) U __stdcall LocalFree(void*);\n'
                      'void run_file_test(void); void run_registry_test(void);\n'
                      'void mainCRTStartup(void) {\n'
                      'run_file_test(); run_registry_test();\n'
                      'U values[4]={1,2,3,4}; U *cursor=values; char *out=0;\n'
                      'U n=FormatMessageA(0x500,"%1!d!%.%2!d!%.%3!d!%.%4!d!",0,0,&out,0,&cursor);\n'
                      'const char *expected="1.2.3.4";\n'
                      'if(n!=7 || !out) ExitProcess(530);\n'
                      'for(U i=0;i<8;++i) if(out[i]!=expected[i]) ExitProcess(531);\n'
                      'if(cursor!=values+4) ExitProcess(532);\n'
                      'if(LocalFree(out)) ExitProcess(533);\n'
                      'ExitProcess(0); }\n')
    checks += 4
    subprocess.run(['clang', '--target=i686-pc-windows-msvc', '-O1', '-ffreestanding', '-c', str(source),
                    '-o', str(obj)], env=env, check=True)
    subprocess.run(['lld-link', '/entry:mainCRTStartup', '/subsystem:console', '/timestamp:0', '/nodefaultlib',
                    '/machine:x86', f'/out:{exe}', str(obj), *map(str, objects), *map(str, libraries)], env=env, check=True)
    binary = ROOT / 'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    pristine = win98_c.use_pristine()
    code, text, _ = host(binary, out, 'guest', 'E:\\LEGACY.EXE', 'LEGACY.EXE', [], 20,
                         pristine, win98_c.journal(pristine), drives=(f'E={out}',))
    ok = code == 0 and 'NEO98 HOST exited code=0 ' in text and not failures(text)
    if not ok:
        print(text[-6000:])
    print(f'legacy files host: {checks if ok else 0} of {checks} checks passed')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
