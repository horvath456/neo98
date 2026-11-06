#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Run the named MenuHelp export and Unicode status provider through real x86 calls."""
from pathlib import Path
import argparse,ast,json,shutil,subprocess,sys,hashlib

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


ROOT=Path(__file__).resolve().parents[4] if 'runtime' in str(Path(__file__).parent) else Path(__file__).resolve().parents[1];OUT=ROOT/'build/menu-help';sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4},'USER32':{'CreateWindowExA':48,'SendMessageA':16,'DestroyWindow':4,'CreateMenu':0,'AppendMenuA':16,'DestroyMenu':4},'COMCTL32':{'MenuHelp':28}}
def test(source_root=ROOT):
 source_root=Path(source_root).resolve();OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,names in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 folder=Path(__file__).parent;obj=OUT/'fixture.obj';exe=OUT/'MENUHELP.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/'menu_help_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 tree=ast.parse((ROOT/'tools/native_app.py').read_text());sources=next(_native_app().runtime_names(ast.literal_eval(n.value))for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-menu-help-native'
 build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined', *_native_app().engine_flags(),str(folder/'test_menu_help_native.cpp'),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), *_native_app().unicorn_link(library),'-o',str(binary)],env=env,capture_output=True,text=True);(OUT/'fixture-build.log').write_text(build.stdout+build.stderr);build.check_returncode();run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt')],env=env,capture_output=True,text=True);(OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':['named MenuHelp through x86 stdcall','ignored message preserves status','separator clears simple text','end-of-menu restores multipart status','Unicode Euro and umlaut through original ACP table','255-character capacity and failed-overflow preservation','window and GDI teardown']};(OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);a=p.parse_args();test(a.source_root)
