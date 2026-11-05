#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Queued absolute coordinates cannot turn a small mouse step into a jump.

Exercises SDL -> USER -> guest WM_MOUSEMOVE at scales 1 and 2. Four-component
motion=x,y,dx,dy supplies an SDL event captured before the latest guest warp.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
import neo98
import win98_c
from native_app import build_host
win98_c.use_pristine()

def main():
    out=ROOT/'build/host-mouse-delta';out.mkdir(exist_ok=True)
    env=neo98.tool_environment()
    imports={'KERNEL32':{'GetModuleHandleA':4,'ExitProcess':4},'USER32':{
        'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'GetMessageA':16,
        'DispatchMessageA':4,'SetCursorPos':8,'SetTimer':16,'SetWindowTextA':8}}
    libs=[]
    for dll,names in imports.items():
        definition=out/(dll+'.def');lib=out/(dll+'.lib')
        definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{s}\n' for n,s in names.items()))
        subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True)
        libs.append(str(lib))
    obj=out/'fixture.obj';exe=out/'DELTA.EXE'
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(ROOT/'tools/host_mouse_delta_fixture.c'),'-o',str(obj)],env=env,check=True)
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/nodefaultlib','/machine:x86','/out:'+str(exe),str(obj),*libs],env=env,check=True)
    host=ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    for scale in (1,2):
        drive=out/f'drive-c-{scale}'
        if not drive.exists():subprocess.run(['cp','-c','-R',str(win98_c.FOLDER.resolve()),str(drive)],check=True)
        steps=['0:wait=HOST_MOUSE_DELTA|delta ready','0:idle=1200']
        # Old absolute positions at four edges, but the hand moved only one
        # canvas pixel; zero-motion focus-entry samples must add nothing.
        for x,y,dx,dy in [(0,250,-1,0),(250,0,0,-1),(799*scale,250,1,0),(250,599*scale,0,1),(0,0,0,0)]:
            steps+=['0:key=5a','0:wait=HOST_MOUSE_DELTA|delta ready',
                    f'0:motion={x},{y},{dx*scale},{dy*scale}','0:idle=120','0:key=50',f'0:wait=HOST_MOUSE_DELTA|delta {dx},{dy}']
        if scale==2:
            # A half canvas pixel is kept across a timer's SetCursorPos.
            steps+=['0:key=5a','0:wait=HOST_MOUSE_DELTA|delta ready','0:motion=0,250,-1,0','0:idle=120','0:motion=0,250,-1,0','0:idle=120','0:key=50','0:wait=HOST_MOUSE_DELTA|delta -1,0']
        # Once the guest stops warping, ordinary absolute input still applies.
        steps+=['0:key=48','0:wait=HOST_MOUSE_DELTA|delta free','0:idle=3500','0:key=5a','0:wait=HOST_MOUSE_DELTA|delta ready',f'0:motion={390*scale},{290*scale}','0:idle=120','0:key=50','0:wait=HOST_MOUSE_DELTA|delta -10,-10','0:menu=quit']
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_CURSOR_TRACE='1',NEO98_HOST_SCREEN='2200x1500',NEO98_HOST_INPUT=';'.join(steps),NEO98_HOST_DEADLINE='45',NEO98_SETTINGS_DIR=str(out/'settings'))
        env.pop('NEO98_HOST_MENU',None)
        if '--unicorn' in sys.argv:env['NEO98_CPU']='unicorn'
        log=out/f'scale-{scale}{"-unicorn" if "--unicorn" in sys.argv else ""}.log'
        with log.open('w') as f:
            run=subprocess.run([str(host),'-',str(ROOT/'build/generated/notepad-fixedsys.fnt'),'--system-tree','--exe',str(exe),'--drive-c',str(drive),'--no-registry-file','--scale',str(scale),'--quiet'],env=env,stdout=f,stderr=subprocess.STDOUT)
        text=log.read_text()
        assert run.returncode==0 and 'timed out' not in text and 'deadline' not in text,text[-3000:]
        assert 'wait=HOST_MOUSE_DELTA|delta -10,-10 reached' in text,text[-3000:]
        print(f'MOUSE DELTA PASS scale {scale}: four edges, zero motion, fractions, absolute desktop input')
if __name__=='__main__':main()
