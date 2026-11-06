#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Two processes over one namespace of named kernel objects, under ASan/UBSan."""
from pathlib import Path
import argparse,ast,hashlib,json,shutil,subprocess,sys

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


SOURCE=Path(__file__).resolve().parents[1];ROOT=Path(__file__).resolve().parents[4] if 'runtime' in str(Path(__file__).parent) else SOURCE;OUT=ROOT/'build/named-objects'
IMPORTS={'KERNEL32':{'CreateEventA':16,'OpenEventA':12,'SetEvent':4,'ResetEvent':4,'CreateMutexA':12,'OpenMutexA':12,'ReleaseMutex':4,'CreateSemaphoreA':16,'OpenSemaphoreA':12,'ReleaseSemaphore':12,'CreateFileMappingA':24,'OpenFileMappingA':12,'MapViewOfFile':20,'UnmapViewOfFile':4,'FlushViewOfFile':8,'WaitForSingleObject':8,'WaitForMultipleObjects':16,'CloseHandle':4,'GetLastError':0,'SetLastError':4,'CreateProcessA':40,'GetExitCodeProcess':8,'ExitProcess':4,'GetCommandLineA':0,'Sleep':4,'GetCurrentProcessId':0}}
def test(source_root=SOURCE):
 sys.path.insert(0,str(ROOT));import neo98
 source_root=Path(source_root).resolve();OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,names in IMPORTS.items():
  d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(b)+'\n' for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 folder=source_root/'tools';obj=OUT/'fixture.obj';exe=OUT/'NAMED.EXE'
 subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/'named_objects_fixture.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 tree=ast.parse((source_root/'tools/native_app.py').read_text());sources=next(_native_app().runtime_names(ast.literal_eval(n.value))for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent
 library=package/'lib'/('libunicorn.2.dylib' if sys.platform=='darwin' else 'libunicorn.so.2');binary=OUT/'test-named-objects-native'
 command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined', *_native_app().engine_flags(),str(folder/'test_named_objects_native.cpp'),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), *_native_app().unicorn_link(library),'-o',str(binary)]
 inputs=[source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S'),folder/'test_named_objects_native.cpp',folder/'named_objects_fixture.c',source_root/'tools/native_app.py',*list((source_root/'src').glob('*.h')),*list((source_root/'src').glob('*.inc')),*[source_root/'src'/f'{s}.cpp'for s in sources],*list((source_root/'build/generated').glob('*.h')),source_root/'build/crt-locale/crt_locale_data.h',source_root/'build/image-resource/system_assets.h',*list((source_root/'third_party/unicorn/include').rglob('*.h'))]
 def hashes():return {str(p.relative_to(source_root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(inputs))}
 assert all(not p.is_symlink() for p in inputs);before=hashes()
 build=subprocess.run(command,env=env,capture_output=True,text=True);(OUT/'fixture-build.log').write_text(build.stdout+build.stderr);build.check_returncode();assert before==hashes()
 run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True)
 (OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout[-4000:]+run.stderr,end='');run.check_returncode()
 report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':[
  'one namespace for events, mutexes, semaphores and file mappings; a foreign type is ERROR_INVALID_HANDLE',
  'a second create returns a handle to the same object and sets ERROR_ALREADY_EXISTS',
  'an absent name and an over-long name are ERROR_INVALID_NAME in all four Open entries',
  'names of 260 characters are accepted and compared case-sensitively',
  'a child opens the parent events, mutex, semaphore and mapping by name',
  'a wait in one process wakes when the other signals, single and multiple',
  'the mutex the parent holds is refused to the child and free after its release',
  'the shared block carries bytes in both directions across a flush',
  'an inheritable handle keeps its value in the child and means the same object',
  'CreateProcessA accepts the priority classes 0x1E0 and refuses the rest',
  'the object dies with the last handle of the session, not with the process']}
 (OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=SOURCE);test(p.parse_args().source_root)
