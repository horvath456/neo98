# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
import sys,subprocess,hashlib,json,shutil
SOURCE=Path(__file__).resolve().parents[1]
ROOT=next(p for p in Path(__file__).resolve().parents if (p/'neo98.py').is_file())
OUT=ROOT/'build/message-box-public-repro';OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT));import neo98
E=neo98.tool_environment()
libs=[]
for dll,names in {'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4,'GetLastError':0,'SetLastError':4},'USER32':{'MessageBoxIndirectA':4,'MessageBoxIndirectW':4,'MessageBoxExW':20,'MessageBoxW':16}}.items():
 d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(b)+'\n'for n,b in names.items()));subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(d),'-l',str(lib)],env=E,check=True);libs.append(str(lib))
obj=OUT/'fixture.obj';res=OUT/'fixture.res';exe=OUT/'MBPUBLIC.EXE'
shutil.copyfile(SOURCE/'tools/message_box_public_fixture.rc',OUT/'message_box_public_fixture.rc')
subprocess.run(['llvm-rc','/FO',res.name,'message_box_public_fixture.rc'],cwd=OUT,env=E,check=True)
subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(SOURCE/'tools/message_box_public_fixture.c'),'-o',str(obj)],env=E,check=True)
subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),str(res),*libs],env=E,check=True)
report={'status':'BUILT; native execution pending root integration','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'interactive_steps':6,'input':'Send real Enter press/release once each PublicPhase1..6 has a visible ready dialog. Phase2 selects Cancel; Phase4 selects No. Do not invoke WM_COMMAND directly.','error_codes':[23350,23351,23352,23353,23354,23355,23356]};(OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
