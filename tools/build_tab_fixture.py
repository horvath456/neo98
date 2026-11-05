#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build the focused x86 tab guest; native objects are owned by shared integration."""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
SOURCE=Path(__file__).resolve().parents[1]
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetModuleHandleA':4},'COMCTL32':{'InitCommonControlsEx':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'DestroyWindow':4,'SendMessageA':16,'SetWindowTextA':8,'SetFocus':4,'GetMessageA':16,'TranslateMessage':4,'DispatchMessageA':4}}
def build(root,out):
 sys.path.insert(0,str(root));import neo98
 env=neo98.tool_environment();out.mkdir(parents=True,exist_ok=True);libs=[]
 def run(a):subprocess.run(list(map(str,a)),env=env,check=True)
 for dll,entries in IMPORTS.items():
  definition=out/(dll+'.def');library=out/(dll+'.lib');definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{name}@{count}\n'for name,count in entries.items()));run(['llvm-dlltool','-m','i386','-k','-d',definition,'-l',library]);libs.append(library)
 source=SOURCE/'tools/tab_fixture.c';obj=out/'fixture.obj';exe=out/'TAB.EXE'
 run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',source,'-o',obj]);run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),obj,*libs])
 report={'status':'BUILT; native execution pending parent','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'error_codes':[24800,24839]};(out/'guest-build.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=SOURCE);p.add_argument('--out',type=Path,default=SOURCE/'build/tab-control/guest');a=p.parse_args();build(a.root.resolve(),a.out.resolve())
