#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build only the focused CRT FPU guest; runtime linkage belongs to integration."""
from pathlib import Path
import sys,subprocess,hashlib,json
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/crt-fpu';sys.path.insert(0,str(ROOT));import neo98
OUT.mkdir(parents=True,exist_ok=True);env=neo98.tool_environment();libs=[]
for dll,names in {'KERNEL32':['ExitProcess@4','GetModuleHandleA@4','GetProcAddress@8'],'MSVCRT':['_ftol','_fpreset','_statusfp','_clearfp']}.items():
 d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+'\n'.join(names)+'\n');cmd=['llvm-dlltool','-m','i386','-d',str(d),'-l',str(lib)];cmd+=['-k']if dll=='KERNEL32'else[];subprocess.run(cmd,env=env,check=True);libs.append(str(lib))
source=Path(__file__).resolve().with_name('crt_fpu_fixture.c');obj=OUT/'fixture.obj';exe=OUT/'CRTFPU.EXE'
subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-DCRT_FPU_STANDALONE','-c',str(source),'-o',str(obj)],env=env,check=True)
subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console,4.0','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
report={'status':'BUILT; native execution pending root shared run','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'cases':len(range(11)),'runtime_build':False};(OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
