#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the standalone x86 family fixture and sanitized native runtime in an isolated output."""
from pathlib import Path
import argparse,ast,hashlib,json,subprocess,sys
from native_build import compile_objects
from window_parent_components import ROOT,IMPORTS,neo98

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

P=Path(__file__).resolve()
def test(source_root,out):
 source_root=Path(source_root).resolve();out=Path(out).resolve();out.mkdir(parents=True,exist_ok=True);logs=out/'logs';logs.mkdir(exist_ok=True);env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1';flags=['-std=c++17','-O1','-g','-fsanitize=address,undefined'];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 def run(name,args):
  p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(logs/(name+'.log')).write_text(p.stdout+p.stderr)
  if p.returncode:print(p.stdout+p.stderr)
  p.check_returncode();return p.stdout.strip()
 libs=[]
 for dll,entries in IMPORTS.items():
  d=out/(dll+'.def');l=out/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in entries.items()));run(dll,['llvm-dlltool','-m','i386','-k','-d',d,'-l',l]);libs.append(l)
 run('fixture',['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DWINDOW_PARENT_STANDALONE','-c',source_root/'tools/window_parent_fixture.c','-o',out/'fixture.obj'])
 exe=out/'PARENT.EXE';run('fixture-link',['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),out/'fixture.obj',*libs])
 tree=ast.parse((source_root/'tools/native_app.py').read_text());names=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));sources=[source_root/'src'/(n+'.cpp')for n in names]+[source_root/('src/fiber_x86_64.S' if __import__('platform').machine() == 'x86_64' else 'src/fiber_aarch64.S')]
 paths=compile_objects(source_root,sources,flags,env,ROOT/'build/native-app/object-cache')
 library=ROOT/'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib';binary=out/'test-parent';run('guest-link',['clang++',*flags,source_root/'tools/test_window_parent_guest.cpp',*paths,*_engine_link_extra(), library,'-Wl,-rpath,'+str(library.parent),'-o',binary]);font=ROOT/'build/generated/notepad-fixedsys.fnt';text=run('guest',[binary,exe,font]);result={'status':'PASS','result':text,'sanitizers':['address','undefined'],'runner_sha256':sha(binary),'guest_sha256':sha(exe),'font_sha256':sha(font),'source_root':str(source_root),'runtime_sha256':{str(p.relative_to(source_root)):sha(p)for p in sorted((source_root/'src').iterdir())if p.suffix in('.h','.cpp','.inc','.S')}};(out/'guest-results.json').write_text(json.dumps(result,indent=2)+'\n');print(text);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=P.parents[1]);p.add_argument('--out',type=Path,default=ROOT/'build/window-parent');a=p.parse_args();test(a.source_root,a.out)
