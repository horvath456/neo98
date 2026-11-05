// SPDX-License-Identifier: GPL-2.0-or-later
// A focused child defers the wheel to its parent; capture and pointer belong
// to another window. Verify the real message, modifiers and screen position.
typedef unsigned U;
typedef struct { U hwnd,message,wp,lp,time; int x,y; } MSG;
typedef struct { U style,proc,extra,classExtra,instance,icon,cursor,brush; const char *menu,*name; } CLASS;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(CLASS*);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U SetFocus(U);
API U SetCapture(U);
API U GetMessageA(MSG*,U,U,U);
API U DispatchMessageA(MSG*);
API U SetWindowTextA(U,const char*);
API U SetForegroundWindow(U);
static U parent,child,other,count;
static U __stdcall proc(U h,U m,U wp,U lp)
{
 if(m==0x20a && h==parent){
  const int deltas[]={-120,120,360,-240};
  if(count>=4 || (short)(wp>>16)!=deltas[count] || (wp&65535)!=(count==3?12u:0u) || lp!=(500u|(400u<<16))) ExitProcess(201+count);
  ++count;
  if(count==4) ExitProcess(0);
  SetWindowTextA(parent,count==1?"wheel one":count==2?"wheel two":"wheel three");
  return 0;
 }
 return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void)
{
 CLASS c={0,(U)proc,0,0,0x400000,0,0,0,0,"HOST_WHEEL"};
 if(!RegisterClassA(&c))ExitProcess(200);
 parent=CreateWindowExA(0,c.name,"wheel ready",0x10cf0000,40,40,300,200,0,0,c.instance,0);
 child=CreateWindowExA(0,c.name,"child",0x50000000,10,10,100,60,parent,1,c.instance,0);
 other=CreateWindowExA(0,c.name,"other",0x10cf0000,450,300,200,150,0,0,c.instance,0);
 SetForegroundWindow(parent);SetFocus(child);SetCapture(other);
 // The last wheel must also pass a filtered retrieval while Shift/Ctrl key
 // packets stand ahead of it. They are not dispatched by this retrieval.
 MSG msg;while(GetMessageA(&msg,count==3?child:0,count==3?0x20a:0,count==3?0x20a:0))DispatchMessageA(&msg);
 ExitProcess(202);
}
