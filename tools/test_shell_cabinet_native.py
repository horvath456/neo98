# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
import json,shutil,subprocess,sys,hashlib
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').exists());SOURCE=Path(__file__).resolve().parents[1];OUT=ROOT/'build/shell-cabinet'
IMPORTS={'KERNEL32':{'ExitProcess':4,'GetCommandLineA':0,'Sleep':4,'GetLastError':0},'ADVAPI32':{'RegCreateKeyExA':36,'RegSetValueExA':24,'RegQueryValueExA':24,'RegCloseKey':4},'SHELL32':{'O654':8,'O651':8,'O652':4,'O244':8,'SHGetSettings':8}}
def build_guest(source_root=SOURCE):
 sys.path.insert(0,str(ROOT));import neo98
 env=neo98.tool_environment();dlltool=shutil.which('llvm-dlltool',path=env['PATH']);libs=[];OUT.mkdir(parents=True,exist_ok=True)
 for dll,names in IMPORTS.items():
  d=OUT/(dll+'.def');lib=OUT/(dll+'.lib');d.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(n+'@'+str(b)+(' @'+n[1:]+' NONAME'if n.startswith('O')else'')+'\n'for n,b in names.items()));subprocess.run([dlltool,'-m','i386','-k','-d',str(d),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
 obj=OUT/'fixture.obj';exe=OUT/'CABINET.EXE';subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(Path(source_root)/'tools/shell_cabinet_fixture.c'),'-o',str(obj)],env=env,check=True);subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:console','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True);report={'status':'GUEST BUILT; native execution pending shared integration','guest_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'native_argc':3,'arguments':['runner','CABINET.EXE','Fixedsys']};(OUT/'guest-results.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(build_guest())
