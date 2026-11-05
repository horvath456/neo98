/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);API U GetModuleHandleA(const char*);API U GetLastError(void);API void SetLastError(U);
typedef struct {U size,owner,instance;const char*text,*caption;U style,icon,context,callback,language;} P;
API U MessageBoxIndirectA(const P*);API U MessageBoxIndirectW(const void*);
API U MessageBoxExW(U,const W*,const W*,U,U);API U MessageBoxW(U,const W*,const W*,U);
__declspec(dllexport) volatile U PublicPhase;
static void check(U ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){P p={40,0,0,(const char*)501,(const char*)500,0,0,0,0,0x409};p.instance=GetModuleHandleA(0);
 PublicPhase=1;check(MessageBoxIndirectA(&p)==1,23350);
 p.text=(const char*)503;p.caption=(const char*)502;p.style=0x101;
 PublicPhase=2;check(MessageBoxIndirectA(&p)==2,23351);
 p.text=(const char*)505;p.caption=(const char*)504;p.style=0;
 PublicPhase=3;check(MessageBoxIndirectA(&p)==1,23352);
 static const W title[]={'W','i','d','e',0},text[]={'G','r',0xfc,0xdf,'e',' ',0x20ac,0};
 PublicPhase=4;check(MessageBoxExW(0,text,title,0x103,0x409)==7,23353);
 PublicPhase=5;check(MessageBoxW(0,text,title,0)==1,23354);
 p.instance=0;p.text=(const char*)17;p.caption=(const char*)18;
 PublicPhase=6;check(MessageBoxIndirectA(&p)==1,23355);
 SetLastError(0);check(MessageBoxIndirectW((const void*)0xdeadc0de)==0&&GetLastError()==120,23356);
 PublicPhase=7;ExitProcess(0);
}
