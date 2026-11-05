#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Send fractional SDL wheel events through USER to a real x86 window procedure."""
import os
from pathlib import Path
import subprocess
from native_app import ROOT
import neo98
import win98_c


def main():
    win98_c.use_pristine();out=ROOT/'build/native-linux/scroll';out.mkdir(parents=True,exist_ok=True)
    env=neo98.tool_environment();libraries=[]
    imports={'KERNEL32':{'ExitProcess':4},'USER32':{'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,
        'SetFocus':4,'SetCapture':4,'GetMessageA':16,'DispatchMessageA':4,'SetWindowTextA':8,'SetForegroundWindow':4}}
    for dll,names in imports.items():
        definition,library=out/(dll+'.def'),out/(dll+'.lib')
        definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{name}@{size}\n' for name,size in names.items()))
        subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(library)],env=env,check=True);libraries.append(str(library))
    fixture=out/'fixture.c'
    fixture.write_text((ROOT/'tools/host_wheel_fixture.c').read_text().replace('(count==3?12u:0u)','0u'))
    obj,exe=out/'fixture.obj',out/'WHEEL.EXE'
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(fixture),'-o',str(obj)],env=env,check=True)
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libraries],env=env,check=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_CLIPBOARD='private',
        NEO98_HOST_INPUT='1:wait=HOST_WHEEL;1.1:move=500,400;2:wheel=-0.25;2.1:wheel=-0.25;2.2:wheel=-0.25;2.3:wheel=-0.25;3:wheel=1;4:wheel=3;5:wheel=-2')
    with (out/'host.log').open('w') as log:
        result=subprocess.run([str(ROOT/'build/native-app/neo98-host'),'--exe',str(exe),'--ram-drive','--no-registry-file','--no-seamless','--time-limit','12','--verbose'],env=env,cwd=ROOT,stdout=log,stderr=log,timeout=25)
    text=(out/'host.log').read_text()
    assert result.returncode==0 and 'NEO98 Linux state=2 ' in text,text[-3500:]
    print('LINUX SCROLL GUEST PASS: SDL fractions, both directions, bursts, focused child and captured parent delivery')

if __name__=='__main__':main()
