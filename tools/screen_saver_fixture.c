// SPDX-License-Identifier: GPL-2.0-or-later
// Guest for tools/test_screen_saver_host.py: sets the screen saver to
// three seconds and the display's power off to eight, both active, keeps a
// window in front and pumps messages for a minute. USER then starts SYSTEM.INI
// [boot] SCRNSAVE.EXE through SC_SCREENSAVE, and the display goes black.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
API void S ExitProcess(U);API U S GetTickCount(void);API void S Sleep(U);
API U S RegisterClassA(struct WndClass*);API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DefWindowProcA(U,U,U,U);API U S ShowWindow(U,int);API U S SystemParametersInfoA(U,U,void*,U);
API U S PeekMessageA(void*,U,U,U,U);API U S DispatchMessageA(const void*);API U S TranslateMessage(const void*);
static U S wndproc(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
void mainCRTStartup(void)
{
 struct WndClass klass={0};U msg[7],start;
 klass.proc=(U)wndproc;klass.instance=0x400000;klass.brush=6;klass.name=(U)"Neo98Saver";
 if(!RegisterClassA(&klass))ExitProcess(1);
 U w=CreateWindowExA(0,"Neo98Saver","Screen saver test",0x10cf0000u,40,40,300,200,0,0,0x400000,0);
 if(!w)ExitProcess(2);
 if(!SystemParametersInfoA(15,3,0,0)||!SystemParametersInfoA(17,1,0,0))ExitProcess(3);
 if(!SystemParametersInfoA(0x52,8,0,0)||!SystemParametersInfoA(0x56,1,0,0))ExitProcess(4);
 start=GetTickCount();
 while(GetTickCount()-start<60000){
  while(PeekMessageA(msg,0,0,0,1)){TranslateMessage(msg);DispatchMessageA(msg);}
  Sleep(20);
 }
 ExitProcess(0);
}
