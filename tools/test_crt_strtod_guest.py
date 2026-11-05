#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build only the focused x86 guest; parent owns native runtime linkage."""
from pathlib import Path
import sys,subprocess,hashlib,json
from crt_strtod_components import ROOT,OUT,IMPORTS,CRT_IMPORTS
sys.path.insert(0,str(ROOT));import neo98
OUT.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();libs=[]
for dll,functions in IMPORTS.items():
 d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(size)+'\n'for n,size in functions.items()));subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
d=OUT/'MSVCRT.def';lib=OUT/'MSVCRT.lib';d.write_text('LIBRARY MSVCRT.DLL\nEXPORTS\n'+'\n'.join(CRT_IMPORTS)+'\n');subprocess.run(['llvm-dlltool','-m','i386','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
source=Path(__file__).resolve().with_name('crt_strtod_fixture.c');obj=OUT/'fixture.obj';exe=OUT/'STRTOD.EXE'
subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DCRT_STRTOD_STANDALONE','-c',str(source),'-o',str(obj)],env=env,check=True)
subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console,4.0','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
report={'status':'BUILT; native execution pending root shared run','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'corpus_strings':13,'runtime_build':False};(OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
