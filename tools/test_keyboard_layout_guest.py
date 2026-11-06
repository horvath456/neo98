#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run the x86 keyboard-layout fixture through the ARM Application runtime."""
from pathlib import Path
import argparse,ast,hashlib,json,shutil,subprocess,sys
from keyboard_layout_components import ROOT,OUT,IMPORTS

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

sys.path.insert(0,str(ROOT));import neo98

def test(source_root=ROOT,build=True):
 OUT.mkdir(exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1';libs=[]
 dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,functions in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{name}@{size}\n'for name,size in functions.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 obj=OUT/'fixture.obj';exe=OUT/'KBDLAYOUT.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DKEYBOARD_LAYOUT_STANDALONE','-c',str(Path(__file__).with_name('keyboard_layout_fixture.c')),'-o',str(obj)],env=env,check=True)
 subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 source_root=Path(source_root).resolve();source=Path(__file__).with_name('test_keyboard_layout_guest.cpp')
 if source_root!=ROOT:
  (source_root/'tools').mkdir(exist_ok=True);target=source_root/'tools'/source.name
  if target.is_symlink():target.unlink()

  if source.resolve()!=target.resolve():shutil.copyfile(source,target)
  source=target
 tree=ast.parse((ROOT/'tools/native_app.py').read_text());sources=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets))
 if 'x86_keyboard_layout' not in sources:sources.append('x86_keyboard_layout')
 package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-keyboard-layout-guest'
 if build:
  result=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',str(source),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), str(library),'-Wl,-rpath,'+str(library.parent),'-o',str(binary)],env=env,capture_output=True,text=True);(OUT/'guest-build.log').write_text(result.stdout+result.stderr)
  if result.returncode:print(result.stdout+result.stderr)
  result.check_returncode()
 run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'guest.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode()
 report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'runner_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'source':str(source_root),'result':run.stdout.strip()};(OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--no-build',action='store_true');a=p.parse_args();test(a.source_root,not a.no_build)
