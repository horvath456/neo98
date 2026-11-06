#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Focused guest using existing sanitized runtime objects; no assets are generated."""
from pathlib import Path
import ast,hashlib,json,subprocess,sys
import argparse

def _engine_link_extra():
    """The FEXCore provider for this runner when the host links it
    (tools/native_app.py, engine_link_extra), else nothing."""
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parent))
    import native_app as _native_app
    return [str(p) for p in _native_app.engine_link_extra()]

P=Path(__file__).resolve();ROOT=next(p for p in P.parents if(p/'neo98.py').exists()and(p/'build/analysis-venv').exists());sys.path.insert(0,str(ROOT));import neo98
parser=argparse.ArgumentParser();parser.add_argument('--source-root',type=Path,default=P.parents[1]);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--objects',type=Path,required=True);parser.add_argument('--rebuild-affected',action='store_true');args=parser.parse_args();RUN=args.source_root.resolve();OUT=args.out.resolve();OUT.mkdir(parents=True,exist_ok=True);OBJECTS=args.objects.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();env=neo98.tool_environment();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1';FLAGS=['-std=c++17','-O1','-g','-fsanitize=address,undefined'];GUEST=OUT;GUEST.mkdir(exist_ok=True)
def run(name,args):
 p=subprocess.run(list(map(str,args)),env=env,capture_output=True,text=True);(GUEST/(name+'.log')).write_text(p.stdout+p.stderr)
 if p.returncode:print(p.stdout+p.stderr)
 p.check_returncode();return p.stdout.strip()
sys.path.insert(0,str(RUN/'tools'));from device_color_components import IMPORTS as imports
libs=[]
for dll,entries in imports.items():
 d=GUEST/(dll+'.def');l=GUEST/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in entries.items()));run(dll,['llvm-dlltool','-m','i386','-k','-d',d,'-l',l]);libs.append(l)
run('fixture',['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DDEVICE_COLOR_STANDALONE','-c',RUN/'tools/device_color_fixture.c','-o',GUEST/'fixture.obj'])
exe=GUEST/'DEVCOLOR.EXE';run('fixture-link',['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),GUEST/'fixture.obj',*libs])
tree=ast.parse((RUN/'tools/native_app.py').read_text());names=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));objects=[OBJECTS/(n+'.cpp.o')for n in names]+[OBJECTS/('fiber_x86_64.S.o' if __import__('platform').machine() == 'x86_64' else 'fiber_aarch64.S.o')]
for n in (['gdi','x86_gdi'] if args.rebuild_affected else []):run(n,['clang++',*FLAGS,'-c',RUN/'src'/(n+'.cpp'),'-o',OBJECTS/(n+'.cpp.o')])
library=ROOT/'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib';binary=GUEST/'test-device-color';run('guest-link',['clang++',*FLAGS,RUN/'tools/test_device_color_guest.cpp',*objects,*_engine_link_extra(), library,'-Wl,-rpath,'+str(library.parent),'-o',binary]);font=ROOT/'build/generated/notepad-fixedsys.fnt';result=run('guest',[binary,exe,font]);print(result)
(GUEST/'results.json').write_text(json.dumps({'status':'PASS','result':result,'runner_sha256':sha(binary),'guest_sha256':sha(exe),'font_sha256':sha(font),'runtime_sha256':{str(p.relative_to(RUN)):sha(p)for p in sorted((RUN/'src').iterdir())if p.suffix in('.h','.cpp','.inc','.S')}},indent=2)+'\n')
