#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise original Shell registry wrappers and Unicode adapter through native x86 gates."""
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


ROOT=Path(__file__).resolve().parents[4] if 'runtime' in str(Path(__file__).parent) else Path(__file__).resolve().parents[1];OUT=ROOT/'build/shell-660';sys.path.insert(0,str(ROOT));import neo98
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetCommandLineA':0},'ADVAPI32':{'RegCreateKeyExA':36,'RegSetValueExA':24,'RegCloseKey':4},'SHELL32':{'O660':4,'O71':8},'COMCTL32':{'ImageList_Create':20,'ImageList_Destroy':4,'ImageList_Remove':8,'ImageList_SetIconSize':12,'ImageList_GetImageCount':4,'ImageList_GetIconSize':12,'ImageList_Draw':24},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'PostQuitMessage':4,'GetMessageA':16,'TranslateMessage':4,'DispatchMessageA':4,'BeginPaint':8,'EndPaint':8,'InvalidateRect':12},'GDI32':{'TextOutA':20}}

def test(source_root=ROOT):
 source_root=Path(source_root).resolve();OUT.mkdir(exist_ok=True);env=neo98.tool_environment();libs=[];dlltool=shutil.which('llvm-dlltool',path=env['PATH']);assert dlltool
 for dll,names in IMPORTS.items():
  definition=OUT/(dll+'.def');lib=OUT/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}'+(' @'+n[1:]+' NONAME' if n.startswith('O') else '')+'\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 folder=Path(__file__).parent;obj=OUT/'fixture.obj';exe=OUT/'SHELL660.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(folder/'shell_660_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
 tree=ast.parse((source_root/'tools/native_app.py').read_text());sources=next(_native_app().runtime_names(ast.literal_eval(n.value))for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));sources += [s for s in ['x86_profile','profile_files','x86_shell_search','x86_shell_path_syntax'] if s not in sources];package=Path(subprocess.check_output([str(neo98.analysis_python()),'-c','import unicorn;print(unicorn.__file__)'],text=True).strip()).parent;library=package/'lib'/('libunicorn.2.dylib'if sys.platform=='darwin'else'libunicorn.so.2');binary=OUT/'test-shell-660-native'
 build=subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined', *_native_app().engine_flags(),str(folder/'test_shell_660_native.cpp'),str(source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')),*[str(source_root/'src'/f'{s}.cpp')for s in sources],*_engine_link_extra(), *_native_app().unicorn_link(library),'-o',str(binary)],env=env,capture_output=True,text=True);(OUT/'fixture-build.log').write_text(build.stdout+build.stderr);build.check_returncode();run=subprocess.run([str(binary),str(exe),str(ROOT/'build/generated/notepad-fixedsys.fnt'),str(ROOT/'build/programs/SHELL32.DLL'),str(OUT/'icons32.ppm'),str(OUT/'icons40.ppm')],env=env,capture_output=True,text=True);(OUT/'fixture-native.log').write_text(run.stdout+run.stderr);print(run.stdout+run.stderr,end='');run.check_returncode();report={'status':'PASS','sanitizers':['address','undefined'],'guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':['original45 paired SHELL32 resources in ordinary shared ImageLists','visible real WM_PAINT/ImageList_Draw','registry settings trigger existing shared-list resize/rebuild','shared list handles and pixels survive Application destruction','ordinary private ImageLists use the same new operations','missing resources and existing disk-cache stream fail explicitly and clean shared state']};(OUT/'fixture-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);a=p.parse_args();test(a.source_root)
