#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the focused CPL guest; link against caller-supplied coherent runtime objects."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

SOURCE=Path(__file__).resolve().parents[1];ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').exists());OUT=ROOT/'build/shell-control'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4,'LoadLibraryA':4,'GetProcAddress':8,'FreeLibrary':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'SendMessageA':16},'SHELL32':{'Control_RunDLL':16,'Control_RunDLLA':16}}
def guests(source_root=SOURCE):
 sys.path.insert(0,str(ROOT));import neo98
 source_root=Path(source_root).resolve();folder=source_root/'tools';env=neo98.tool_environment();OUT.mkdir(exist_ok=True);dlltool=shutil.which('llvm-dlltool',path=env['PATH']);libs=[]
 for dll,names in IMPORTS.items():
  d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(b)+'\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 for source,name,dll in [('shell_control_fixture.c','CPLTEST.EXE',False),('shell_control_applet.c','FIXTURE.CPL',True)]:
  obj=OUT/(source+'.obj');subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/source),'-o',str(obj)],env=env,check=True)
  command=['lld-link','/entry:'+('DllMain@12'if dll else'mainCRTStartup'),'/subsystem:windows','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(OUT/name),str(obj),*libs]
  if dll:command+=['/dll','/export:CPlApplet=_CPlApplet@16','/export:FixtureQuery=_FixtureQuery@8']
  subprocess.run(command,env=env,check=True)
 return env

def test(source_root=SOURCE,object_dir=None,app_object=None):
 env=guests(source_root)
 if object_dir is None:return {'status':'GUESTS READY','native_validation':'deferred to shared object integration'}
 source_root=Path(source_root).resolve();object_dir=Path(object_dir).resolve();objects=sorted(object_dir.glob('*.o'));assert objects
 if app_object:objects=[Path(app_object).resolve()if p.name=='x86_app.cpp.o'else p for p in objects]
 def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
 inputs=[*objects,source_root/'tools/test_shell_control_native.cpp',source_root/'src/x86_app.h'];before={str(p):digest(p)for p in inputs}
 sys.path.insert(0,str(ROOT));import neo98
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-shell-control-native'
 command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(source_root/'tools/test_shell_control_native.cpp'),*map(str,objects),*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)]
 result=subprocess.run(command,env=env,capture_output=True,text=True);(OUT/'fixture-link.log').write_text(result.stdout+result.stderr);result.check_returncode();assert before=={str(p):digest(p)for p in inputs}
 run=subprocess.run([str(binary),str(OUT/'CPLTEST.EXE'),str(OUT/'FIXTURE.CPL'),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','sanitizers':['address','undefined'],'inputs_unchanged':True,'objects':len(objects),'guest_sha256':digest(OUT/'CPLTEST.EXE'),'applet_sha256':digest(OUT/'FIXTURE.CPL'),'checks':['real CPL DLL and CPlApplet callbacks','nested launch reuses cache and balances references','name/@index/default selection','STARTWPARMSA and DBLCLK fallback','INIT rejection and STOP/EXIT order','caller command unchanged','visible owner and original owner notification','final window/module cleanup']};(OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');(OUT/'linked-inputs.json').write_text(json.dumps(before,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=SOURCE);p.add_argument('--object-dir',type=Path);p.add_argument('--app-object',type=Path);a=p.parse_args();print(test(a.source_root,a.object_dir,a.app_object))
