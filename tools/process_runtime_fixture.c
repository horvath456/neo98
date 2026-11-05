/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
typedef unsigned U;typedef struct{U hwnd,message,wp,lp,time;int x,y;} MSG;typedef struct{U style,proc,extra,classExtra,instance,icon,cursor,brush;const char*menu,*name;} CLASS;
void*memset(void*p,int c,U n){unsigned char*q=p;for(U i=0;i<n;++i)q[i]=c;return p;}
#define API __declspec(dllimport) __stdcall
API U GetLastError(void);API U SetParent(U,U);API U SendMessageA(U,U,U,U);API void ExitProcess(U);API char*GetCommandLineA(void);API U GetCurrentProcessId(void);API U GetCurrentThreadId(void);API U GetModuleHandleA(const char*);API U VirtualAlloc(U,U,U,U);API U GlobalAlloc(U,U);
API U RegisterClassA(CLASS*);API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);API U DefWindowProcA(U,U,U,U);API U SetWindowTextA(U,const char*);API U GetWindowThreadProcessId(U,U*);API U GetMessageA(MSG*,U,U,U);API U TranslateMessage(MSG*);API U DispatchMessageA(MSG*);
API U GetCursorPos(int*);API U SetCursorPos(int,int);API void mouse_event(U,U,U,U,U);API U ClipCursor(const int*);
API U SetPropA(U,const char*,U);API U GetPropA(U,const char*);API U OpenClipboard(U);API U CloseClipboard(void);API U EmptyClipboard(void);API U SetClipboardData(U,U);API U GetClipboardData(U);
static U identity,count,instance;static volatile U*secret;static const char klass[]="DUAL_SAME_CLASS";
static void check(U v,U code){if(!v)ExitProcess(code);}
static U __stdcall proc(U h,U m,U wp,U lp){
 if(m==1){check(SetPropA(h,"process-owned",identity),15317);return 0;}
 if(m==0x100&&wp=='K'){check(identity==2&&*secret==0xaabb0002,15318);SetWindowTextA(h,"Bkey");return 0;}
 if(m==0x406){check(identity==1&&OpenClipboard(0),15319);char*p=(char*)GetClipboardData(1);check(p&&p[0]=='B'&&p[6]=='d'&&CloseClipboard(),15320);SetWindowTextA(h,"Acopied");return 0;}
 if(m==0x407){check(identity==1&&!SetParent(h,wp)&&GetLastError()==50,15321);check(SendMessageA(wp,0x408,0,0)==0xfeed0002,15322);SetWindowTextA(h,"Aboundary");return 0;}
 // Answered in the owning process: the reply carries that process's identity
 // and its private page, so a synchronous send really crossed the boundary.
 if(m==0x408){check(*secret==0xaabb0000+identity,15323);return 0xfeed0000+identity;}
 if(m==0x409){int p[2];check(identity==2&&GetCursorPos(p)&&p[0]==350&&p[1]==100,15324);check(SetCursorPos(360,110),15325);SetWindowTextA(h,"Bcursor");return 0;}
 if(m==0x40a){int p[2],clip[4]={370,120,380,130};check(identity==1&&GetCursorPos(p)&&p[0]==360&&p[1]==110,15326);mouse_event(1,5,7,0,0);check(GetCursorPos(p)&&p[0]==365&&p[1]==117,15327);check(ClipCursor(clip)&&GetCursorPos(p)&&p[0]==370&&p[1]==120&&ClipCursor(0),15328);SetWindowTextA(h,"Acursor");return 0;}
 if(m==0x401){check(wp==identity&&*secret==0xaabb0000+identity,15301);++count;char title[4]={(char)('A'+identity-1),(char)('0'+count),0};SetWindowTextA(h,title);return 0;}
 if(m==0x402){check(identity==1&&*secret==0xaabb0001,15302);ExitProcess(0);return 0;}
 if(m==0x403){check(identity==2&&OpenClipboard(h),15303);check(EmptyClipboard(),15304);char*p=(char*)GlobalAlloc(0x40,16);check((U)p,15305);p[0]='B';p[1]='-';p[2]='o';p[3]='w';p[4]='n';p[5]='e';p[6]='d';p[7]=0;check(SetClipboardData(1,(U)p)&&CloseClipboard(),15306);SetWindowTextA(h,"Bclipboard");return 0;}
 if(m==0x404){check(identity==2&&*secret==0xaabb0002&&GetPropA(h,"process-owned")==2&&OpenClipboard(0),15307);char*p=(char*)GetClipboardData(1);check(p&&p[0]=='B'&&p[6]=='d'&&CloseClipboard(),15308);U second=CreateWindowExA(0,klass,"Bsecond",0x10cf0000,420,80,220,120,0,0,instance,0);check(second,15309);U pid=0;check(GetWindowThreadProcessId(second,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId(),15310);SetWindowTextA(h,"Bsurvived");return 0;}
 if(m==0x405){check(identity==2&&*secret==0xaabb0002,15311);ExitProcess(0);return 0;}
 return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void){char*command=GetCommandLineA();identity=command[0]=='A'?1:2;instance=GetModuleHandleA(0);secret=(volatile U*)VirtualAlloc(0x18000000,4096,0x3000,4);check((U)secret==0x18000000,15312);*secret=0xaabb0000+identity;CLASS c={0,(U)proc,0,0,instance,0,0,6,0,klass};check(RegisterClassA(&c),15313);U h=CreateWindowExA(0,klass,identity==1?"A0":"B0",0x10cf0000,identity==1?20:300,60,240,160,0,0,instance,0);check(h,15314);U pid=0;check(GetWindowThreadProcessId(h,&pid)==GetCurrentThreadId()&&pid==GetCurrentProcessId(),15315);MSG m;while(GetMessageA(&m,0,0,0)){TranslateMessage(&m);DispatchMessageA(&m);}ExitProcess(15316);}
