#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run the modeless-dialog fixture through the ARM Application runtime under ASan/UBSan."""
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

ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file())
sys.path.insert(0,str(ROOT));import neo98
OUT=ROOT/'build/yield-dialog'
IMPORTS={'KERNEL32':{'GetModuleHandleA':4,'GetLastError':0,'ExitProcess':4},
         'USER32':{'CreateDialogIndirectParamA':20,'RegisterClassA':4,'DefWindowProcA':16,
                   'DestroyWindow':4,'EndDialog':8,'IsWindow':4,'GetParent':4,'GetDlgItem':8}}

def test(source_root=ROOT,build=True):
 OUT.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1';libs=[]
 for dll,functions in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib')
  definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{name}@{size}\n'for name,size in functions.items()))
  subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 obj=OUT/'fixture.obj';exe=OUT/'YIELDDLG.EXE'
 subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(ROOT/'tools/yield_dialog_fixture.c'),'-o',str(obj)],env=env,check=True)
 subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 source_root=Path(source_root).resolve();source=ROOT/'tools/test_yield_dialog_guest.cpp'
 tree=ast.parse(((source_root/'tools/native_app.py')if(source_root/'tools/native_app.py').exists()else(ROOT/'tools/native_app.py')).read_text())
 sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent
 library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-yield-dialog'
 if build:
  result=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(source),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)],env=env,capture_output=True,text=True)
  (OUT/'guest-build.log').write_text(result.stdout+result.stderr)
  if result.returncode:print(result.stdout+result.stderr)
  result.check_returncode()
 run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True)
 (OUT/'guest.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
 report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'runner_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'source':str(source_root),'result':run.stdout.strip()}
 (OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--no-build',action='store_true');a=p.parse_args();test(a.source_root,not a.no_build)
