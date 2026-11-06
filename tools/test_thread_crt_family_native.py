#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Compile the CRT PTD family guest and native runtime without shared generators."""
from pathlib import Path
import argparse
import ast
import json
import os
import subprocess
import sys
import unicorn

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/thread-crt/family/native'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source-root',type=Path,default=ROOT)
    args=parser.parse_args();source=args.source_root.resolve()
    OUT.mkdir(parents=True,exist_ok=True)
    environment=os.environ.copy()
    environment['PATH']='/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:'+environment.get('PATH','')
    environment['UBSAN_OPTIONS']='halt_on_error=1'
    def execute(command,log):
        r=subprocess.run([str(x)for x in command],env=environment,capture_output=True,text=True)
        (OUT/log).write_text(r.stdout+r.stderr)
        if r.returncode:print(r.stdout+r.stderr,end='')
        r.check_returncode()
        return r.stdout
    libraries=[]
    imports={'KERNEL32':['GetCurrentThreadId@0','GetLastError@0','SetLastError@4','Sleep@4',
                        'GetExitCodeThread@8','CloseHandle@4','TlsAlloc@0','TlsFree@4','ExitProcess@4'],
             'MSVCRT':['_beginthread','_endthread','_endthreadex','_errno','__doserrno','_msize','srand','rand','strtok','memcpy','__threadid','__threadhandle']}
    for dll,names in imports.items():
        definition=OUT/(dll+'.def');library=OUT/(dll+'.lib')
        definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+'\n'.join(names)+'\n')
        execute(['llvm-dlltool','-m','i386','-k','-d',definition,'-l',library],dll+'-build.log');libraries.append(library)
    entry=OUT/'entry.c'
    entry.write_text('void run_thread_crt_family_test(void);\n__declspec(dllimport) void __stdcall ExitProcess(unsigned);\nvoid mainCRTStartup(void){run_thread_crt_family_test();ExitProcess(0);}\n')
    objects=[]
    for file in [source/'tools/thread_crt_family_fixture.c',entry]:
        obj=OUT/(file.stem+'.obj');objects.append(obj)
        execute(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-stack-protector',
                 '-Wall','-Wextra','-Werror','-c',file,'-o',obj],file.stem+'-build.log')
    executable=OUT/'THREAD-FAMILY.EXE'
    execute(['lld-link','/machine:x86','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/fixed',
             '/safeseh:no','/timestamp:0',*objects,*libraries,'/out:'+str(executable)],'guest-link.log')
    tree=ast.parse((source/'tools/native_app.py').read_text())
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(
        isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
    sources=list(dict.fromkeys(ast.literal_eval(assignment.value)))
    package=Path(unicorn.__file__).resolve().parent
    library=package/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2')
    binary=OUT/'test-thread-crt-family-native'
    execute(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             source/'tools/test_thread_crt_family_native.cpp',source/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S'),
             *[source/'src'/f'{name}.cpp'for name in sources],*_engine_link_extra(), library,'-Wl,-rpath,'+str(library.parent),
             '-o',binary],'native-build.log')
    fiber=OUT/'test-fiber-alloc'
    execute(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
             source/'tools/test_fiber_alloc.cpp',source/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S'),'-o',fiber],'fiber-build.log')
    print(execute([fiber],'fiber-test.log'),end='')
    result=execute([binary,executable,ROOT/'build/generated/notepad-fixedsys.fnt'],'native-test.log')
    print(result,end='');assert 'THREAD CRT FAMILY NATIVE PASS' in result
    (OUT/'results.json').write_text(json.dumps({'ASan_UBSan':'PASS','guest':'THREAD-FAMILY.EXE',
        'checks':['legacy cdecl workers and automatic handle closure','legacy worker using EndEx keeps its handle',
                  'PTD ID/handle/errno/doserrno and immediate heap release','rand and strtok isolation while workers yield',
                  'all 80 public TLS slots occupied','real native CreateThread failure maps errno/doserrno'],
        'shared_generators':False},indent=2)+'\n')

if __name__=='__main__':main()
