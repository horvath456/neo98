#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the thread-control adapter and x86 fixture in an isolated copy, without generators."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
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
OUT=ROOT/'build/thread-control/native'
sys.path.insert(0,str(ROOT))
import neo98
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--patch',type=Path);args=parser.parse_args()
    environment=neo98.tool_environment()
    os.environ['PATH']=environment['PATH']
    dlltool=shutil.which('llvm-dlltool',path=environment['PATH'])
    if not dlltool:raise RuntimeError('Missing llvm-dlltool on the configured tool PATH')
    OUT.mkdir(parents=True,exist_ok=True);directory=OUT/'runtime'
    if directory.exists():shutil.rmtree(directory)
    directory.mkdir();shutil.copytree(ROOT/'src',directory/'src',ignore=shutil.ignore_patterns('*.o','*.d','*.img','*.elf','*.map','*.lst'))
    (directory/'tools').mkdir();shutil.copy2(ROOT/'tools/native_app.py',directory/'tools/native_app.py')
    shutil.copy2(ROOT/'tools/thread_crt_fixture.c',directory/'tools/thread_crt_fixture.c')
    for name in ('build','third_party'):(directory/name).symlink_to(ROOT/name,target_is_directory=True)
    if args.patch:
        subprocess.run(['git','init','-q'],cwd=directory,check=True)
        subprocess.run(['git','apply','-p2',str(args.patch.resolve())],cwd=directory,check=True)
    for f in ('test_thread_control_native.cpp','thread_control_fixture.c','system_tree_host.h','host_volume.h','host_cd_image.h'):shutil.copy2(ROOT/'tools'/f,directory/'tools'/f)
    imports={'CreateThread':6,'GetCurrentThread':0,'GetCurrentProcess':0,'SuspendThread':1,'ResumeThread':1,
        'GetThreadPriority':1,'SetThreadPriority':2,'GetPriorityClass':1,'SetPriorityClass':2,'SetThreadAffinityMask':2,
        'GetExitCodeThread':2,'GetThreadTimes':5,'GetThreadPriorityBoost':2,'SetThreadPriorityBoost':2,
        'SetThreadIdealProcessor':2,'SwitchToThread':0,'CreateEventA':4,'SetEvent':1,'WaitForSingleObject':2,
        'CloseHandle':1,'DuplicateHandle':7,'InterlockedIncrement':1,'Sleep':1,'SetLastError':1,'GetLastError':0,'ExitProcess':1}
    definition=OUT/'kernel.def';definition.write_text('LIBRARY KERNEL32.DLL\nEXPORTS\n'+''.join(f'{name}@{count*4}\n' for name,count in imports.items()))
    subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(OUT/'kernel.lib')],check=True)
    crt_definition=OUT/'crt.def';crt_definition.write_text('LIBRARY MSVCRT.DLL\nEXPORTS\n_onexit\n_cexit\n')
    subprocess.run([dlltool,'-m','i386','-d',str(crt_definition),'-l',str(OUT/'crt.lib')],check=True)
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-DTHREAD_CONTROL_STANDALONE','-c',str(ROOT/'tools/thread_control_fixture.c'),'-o',str(OUT/'control.obj')],check=True)
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/machine:x86','/out:'+str(OUT/'THREADCTL.EXE'),str(OUT/'control.obj'),str(OUT/'kernel.lib'),str(OUT/'crt.lib')],check=True)
    tree=ast.parse((directory/'tools/native_app.py').read_text())
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='sources' for t in n.targets))
    sources=list(dict.fromkeys(ast.literal_eval(assignment.value)))
    pkg=Path(unicorn.__file__).resolve().parent;lib=pkg/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2')
    command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(directory/'tools/test_thread_control_native.cpp'),str(directory/'src/fiber_aarch64.S'),*[str(directory/'src'/f'{name}.cpp') for name in sources],*_engine_link_extra(), str(lib),'-Wl,-rpath,'+str(lib.parent),'-o',str(OUT/'test-thread-control-native')]
    run=subprocess.run(command,capture_output=True,text=True);(OUT/'build.log').write_text(run.stdout+run.stderr)
    if run.returncode:print(run.stdout+run.stderr)
    run.check_returncode()
    env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
    run=subprocess.run([str(OUT/'test-thread-control-native'),str(OUT/'THREADCTL.EXE'),str(ROOT/'build/generated/notepad-fixedsys.fnt')],capture_output=True,text=True,env=env)
    (OUT/'test.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
    (OUT/'results.json').write_text(json.dumps({'asan_ubsan':'pass','guest_fixture':'thread_control_fixture.c','guest_sha256':hashlib.sha256((OUT/'THREADCTL.EXE').read_bytes()).hexdigest(),'shared_generators':False},indent=2)+'\n')
if __name__=='__main__':main()
