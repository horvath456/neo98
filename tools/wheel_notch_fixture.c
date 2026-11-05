// SPDX-License-Identifier: GPL-2.0-or-later
// A program that only knows whole notches of the wheel: it divides the delta
// of WM_MOUSEWHEEL by WHEEL_DELTA and drops the rest, as many programs of the
// time do. The title says how many notches arrived and in how many messages
// (tools/test_wheel_host.py).
typedef unsigned U;
typedef struct { U hwnd,message,wp,lp,time; int x,y; } MSG;
typedef struct { U style,proc,extra,classExtra,instance,icon,cursor,brush; const char *menu,*name; } CLASS;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(CLASS*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(MSG*,U,U,U);
API U DispatchMessageA(MSG*);
API U SetWindowTextA(U,const char*);
API U SetForegroundWindow(U);
static int notches,messages,small;
static U __stdcall proc(U h,U m,U wp,U lp)
{
 if(m==0x20a){
  const int delta=(short)(wp>>16),whole=delta/120;
  if(!whole){++small;return 0;}
  notches+=whole;++messages;
  char text[40]="notches ";U at=8;int n=notches<0?-notches:notches;
  if(notches<0)text[at++]='-';
  if(n>=10)text[at++]=(char)('0'+n/10);
  text[at++]=(char)('0'+n%10);
  text[at++]=' ';text[at++]='i';text[at++]='n';text[at++]=' ';
  text[at++]=(char)('0'+messages%10);
  text[at++]=' ';text[at++]='s';text[at++]='m';text[at++]='a';text[at++]='l';text[at++]='l';text[at++]=' ';
  if(small>=10)text[at++]=(char)('0'+small/10%10);
  text[at++]=(char)('0'+small%10);text[at]=0;
  SetWindowTextA(h,text);
  return 0;
 }
 return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void)
{
 CLASS c={0,(U)proc,0,0,0x400000,0,0,0,0,"WHEEL_NOTCH"};
 if(!RegisterClassA(&c))ExitProcess(200);
 const U window=CreateWindowExA(0,c.name,"notches ready",0x10cf0000,40,40,400,300,0,0,c.instance,0);
 SetForegroundWindow(window);
 MSG msg;while(GetMessageA(&msg,0,0,0))DispatchMessageA(&msg);
 ExitProcess(0);
}
