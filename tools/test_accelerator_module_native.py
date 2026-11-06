#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Module-qualified accelerator resources through the public guest ABI."""
import os
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT)); sys.path.insert(0, str(ROOT / 'tools'))
import neo98
from native_app import build_host
SOURCE = r'''
/* No C library is linked (/nodefaultlib); some clang versions still call
   these for copies and clears of structures. */
__attribute__((optnone)) void *memcpy(void *d, const void *s, unsigned n)
{ char *o = d; const char *i = s; while (n--) *o++ = *i++; return d; }
__attribute__((optnone)) void *memset(void *d, int c, unsigned n)
{ char *o = d; while (n--) *o++ = (char)c; return d; }
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U LoadLibraryExA(const char *, U, U);
API U GetModuleHandleA(const char *);
API U LoadAcceleratorsA(U, const char *);
API int CopyAcceleratorTableA(U, void *, int);
API U DestroyAcceleratorTable(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API int TranslateAcceleratorA(U,U,void *);
API int SetKeyboardState(void *);
typedef struct { unsigned char flags, pad; unsigned short key, command; } Accel;
typedef struct { U hwnd, message, wparam, lparam, time; int x,y; } Msg;
static U received;
static U __stdcall wnd(U h,U m,U w,U l) {
 if(m==0x111) { received=w; return 0; }
 return DefWindowProcA(h,m,w,l);
}
static void check(int yes,U code) { if(!yes) ExitProcess(code); }
void mainCRTStartup(void) {
 U cls[10]={0,(U)wnd,0,0,0x400000,0,0,0,0,(U)"ACCEL_MODULE"};
 check(RegisterClassA(cls)!=0,1);
 U hwnd=CreateWindowExA(0,"ACCEL_MODULE","accel",0,0,0,100,100,0,0,0x400000,0);
 check(hwnd!=0,2);
 U main=LoadAcceleratorsA(GetModuleHandleA(0),(const char *)1000);
 check(main!=0,3);
 U dll=LoadLibraryExA("C:\\WINDOWS\\SYSTEM\\MSVBVM50.DLL",0,2);
 check(dll!=0,4);
 U vb=LoadAcceleratorsA(dll,(const char *)1000);
 check(vb!=0 && vb!=main,5);
 check(LoadAcceleratorsA(dll,(const char *)1000)==vb,6);
 check(LoadAcceleratorsA(dll,(const char *)999)==0,7);
 check(CopyAcceleratorTableA(main,0,0)==1,8);
 check(CopyAcceleratorTableA(vb,0,0)==74,9);
 Accel table[74];
 check(CopyAcceleratorTableA(vb,table,74)==74,10);
 check(table[26].flags==1 && table[26].key==112 && table[26].command==27,11);
 check(table[73].flags==17 && table[73].key==8 && table[73].command==78,12);
 Msg msg={hwnd,0x100,112,0,0,0,0};
 check(TranslateAcceleratorA(hwnd,vb,&msg)==1 && received==0x1001b,13);
 check(TranslateAcceleratorA(hwnd,main,&msg)==1 && received==0x100c8,14);
 /* FALT: Alt+Backspace arrives as WM_SYSKEYDOWN; F1 with Alt down is not F1. */
 unsigned char keys[256]={0};keys[0x12]=0x80;SetKeyboardState(keys);
 Msg alt={hwnd,0x104,8,0x20000000,0,0,0};
 received=0;
 check(TranslateAcceleratorA(hwnd,vb,&alt)==1 && received==0x1004e,17);
 Msg alt_f1={hwnd,0x104,112,0x20000000,0,0,0};
 received=0;
 check(TranslateAcceleratorA(hwnd,main,&alt_f1)==0 && received==0,18);
 check(DestroyAcceleratorTable(vb)!=0,15);
 check(CopyAcceleratorTableA(vb,0,0)==74,16);
 ExitProcess(0);
}
'''
def main():
    out = ROOT / 'build/accelerator-module'; out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    libs=[]
    imports={'KERNEL32':{'ExitProcess':4,'LoadLibraryExA':12,'GetModuleHandleA':4},
             'USER32':{'LoadAcceleratorsA':8,'CopyAcceleratorTableA':12,'DestroyAcceleratorTable':4,
                       'RegisterClassA':4,'CreateWindowExA':48,'DefWindowProcA':16,'TranslateAcceleratorA':12,'SetKeyboardState':4}}
    for dll,names in imports.items():
        definition,lib=out/(dll+'.def'),out/(dll+'.lib')
        definition.write_text('LIBRARY '+dll+'.DLL\nEXPORTS\n'+''.join(f'{n}@{s}\n' for n,s in names.items()))
        subprocess.run(['llvm-dlltool','-m','i386','-k','-d',str(definition),'-l',str(lib)],env=env,check=True);libs.append(str(lib))
    (out/'fixture.c').write_text(SOURCE)
    (out/'fixture.rc').write_text('1000 ACCELERATORS\nBEGIN\n112, 200, VIRTKEY\nEND\n')
    subprocess.run(['llvm-rc','/fo','fixture.res','fixture.rc'],cwd=out,env=env,check=True)
    subprocess.run(['clang','--target=i686-pc-windows-msvc','-O1','-ffreestanding','-fno-builtin','-fno-stack-protector','-c',str(out/'fixture.c'),'-o',str(out/'fixture.obj')],env=env,check=True)
    exe=out/'ACCELMOD.EXE'
    subprocess.run(['lld-link','/entry:mainCRTStartup','/subsystem:windows','/timestamp:0','/nodefaultlib','/machine:x86','/out:'+str(exe),str(out/'fixture.obj'),str(out/'fixture.res'),*libs],env=env,check=True)
    host=ROOT/'build/native-app/neo98-host' if '--no-build' in sys.argv else build_host()
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',NEO98_HOST_DEADLINE='10',NEO98_HOST_MENU='8:quit')
    env.pop('NEO98_HOST_INPUT',None);env.pop('NEO98_HOST_SHOT',None)
    with (out/'host.log').open('w') as log:
        result=subprocess.run([str(host),'-',str(ROOT/'build/generated/notepad-fixedsys.fnt'),'--system-tree','--exe',str(exe),'--quiet'],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=30)
    text=(out/'host.log').read_text()
    assert result.returncode==0 and 'NEO98 HOST exited code=0 ' in text,text[-4000:]
    print('ACCELERATOR MODULE PASS: same resource ID in EXE and original VB DLL, cached handle, full copy, translation with and without Alt, loaded-table lifetime')
if __name__=='__main__': main()
