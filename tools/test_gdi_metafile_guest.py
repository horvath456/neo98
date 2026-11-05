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
sys.path.insert(0,str(RUN/'tools'));from gdi_metafile_components import IMPORTS as imports
libs=[]
for dll,entries in imports.items():
 d=GUEST/(dll+'.def');l=GUEST/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{b}\n'for n,b in entries.items()));run(dll,['llvm-dlltool','-m','i386','-k','-d',d,'-l',l]);libs.append(l)
run('fixture',['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DGDI_METAFILE_STANDALONE','-c',RUN/'tools/gdi_metafile_fixture.c','-o',GUEST/'fixture.obj'])
exe=GUEST/'WMFTEST.EXE';run('fixture-link',['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),GUEST/'fixture.obj',*libs])
tree=ast.parse((RUN/'tools/native_app.py').read_text());names=next(ast.literal_eval(n.value)for n in ast.walk(tree)if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='sources'for t in n.targets));objects=[OBJECTS/(n+'.cpp.o')for n in names]+[OBJECTS/'fiber_aarch64.S.o']
for n in (['gdi','x86_gdi','x86_app'] if args.rebuild_affected else []):run(n,['clang++',*FLAGS,'-c',RUN/'src'/(n+'.cpp'),'-o',OBJECTS/(n+'.cpp.o')])
library=ROOT/'build/analysis-venv/lib/python3.14/site-packages/unicorn/lib/libunicorn.2.dylib';binary=GUEST/'test-gdi-metafile';run('guest-link',['clang++',*FLAGS,RUN/'tools/test_gdi_metafile_guest.cpp',*objects,*_engine_link_extra(), library,'-Wl,-rpath,'+str(library.parent),'-o',binary]);font=ROOT/'build/generated/notepad-fixedsys.fnt';wmf=GUEST/'runner.wmf';emf=GUEST/'runner.emf';result=run('guest',[binary,exe,font,wmf,emf]);print(result)
import struct
raw=wmf.read_bytes();mtype,hsize,version,size,objects,maximum,parameters=struct.unpack_from('<HHHIHIH',raw,0)
assert (mtype,hsize,version,parameters)==(1,9,0x300,0),(mtype,hsize,version,parameters)
assert size*2==len(raw),(size,len(raw))
at,records,widest=18,0,0
while at<len(raw):
 rsize,function=struct.unpack_from('<IH',raw,at);assert rsize>=3 and at+rsize*2<=len(raw),(at,rsize)
 widest=max(widest,rsize)
 if not function:assert at+6==len(raw);break
 assert function>>8==0 or (rsize-3)>=0
 records+=1;at+=rsize*2
else:raise AssertionError('metafile without its final record')
assert widest==maximum,(widest,maximum)
assert records and objects==0,(records,objects)
print(f'metafile bytes={len(raw)} records={records} maxrecord={maximum}')
enh=emf.read_bytes()
itype,nsize=struct.unpack_from('<II',enh,0)
signature,nversion,nbytes,nrecords,nhandles=struct.unpack_from('<IIIIH',enh,40)
assert (itype,signature,nversion)==(1,0x464d4520,0x10000),(itype,hex(signature),hex(nversion))
assert nbytes==len(enh),(nbytes,len(enh))
assert nsize>=88 and nsize<=len(enh) and nsize%4==0,nsize
at,seen=nsize,1
while at<len(enh):
 rtype,rsize=struct.unpack_from('<II',enh,at);assert rsize>=8 and rsize%4==0 and at+rsize<=len(enh),(at,rtype,rsize)
 seen+=1
 if rtype==14:assert at+rsize==len(enh);break
 at+=rsize
else:raise AssertionError('enhanced metafile without EMR_EOF')
assert seen==nrecords,(seen,nrecords)
assert nhandles>=1
print(f'enhanced bytes={len(enh)} records={nrecords} handles={nhandles}')
(GUEST/'results.json').write_text(json.dumps({'status':'PASS','result':result,'runner_sha256':sha(binary),'guest_sha256':sha(exe),'metafile_sha256':sha(wmf),'metafile_bytes':len(raw),'enhanced_sha256':sha(emf),'enhanced_bytes':len(enh),'font_sha256':sha(font),'runtime_sha256':{str(p.relative_to(RUN)):sha(p)for p in sorted((RUN/'src').iterdir())if p.suffix in('.h','.cpp','.inc','.S')}},indent=2)+'\n')
