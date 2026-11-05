// SPDX-License-Identifier: GPL-2.0-or-later
// Like Explorer: the foreground and captured windows have different threads.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg {U hwnd,message,wp,lp,time;int x,y;};
struct Class {U style,proc,a,b,instance,icon,cursor,brush,menu,name;};
API void S ExitProcess(U);
API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);
API U S CreateFileA(const char*,U,U,U,U,U,U);
API U S WriteFile(U,const void*,U,U*,U);API U S CloseHandle(U);
API U S RegisterClassA(struct Class*);
API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DefWindowProcA(U,U,U,U);API U S ShowWindow(U,int);
API U S SetForegroundWindow(U);API U S GetForegroundWindow(void);
API U S SetCapture(U);API U S GetCapture(void);API U S ReleaseCapture(void);
API U S GetMessageA(struct Msg*,U,U,U);API U S PeekMessageA(struct Msg*,U,U,U,U);
API U S DispatchMessageA(struct Msg*);
static U first;
static U S procedure(U h,U m,U w,U l){return DefWindowProcA(h,m,w,l);}
static U S worker(void *unused)
{
 (void)unused;
 U h=CreateWindowExA(0,"CaptureThread","capture",0x90000000,250,200,150,80,0,0,0x400000,0);
 ShowWindow(h,4); // SW_SHOWNOACTIVATE, foreground stays on thread 1.
 if(GetForegroundWindow()!=first)ExitProcess(504501);
 SetCapture(h);
 if(GetCapture()!=h)ExitProcess(504502);
 U n,file=CreateFileA("C:\\WINDOWS\\TEMP\\INPUT-THREAD.DAT",0x40000000,3,0,2,0,0);
 if(file==~0u||!WriteFile(file,&h,4,&n,0)||n!=4||!CloseHandle(file))ExitProcess(504503);
 struct Msg m;
 // COMCTL32's tab tracking loop asks only for WM_LBUTTONUP.
 while(GetCapture()==h)
  if(PeekMessageA(&m,h,0x202,0x202,1))
  {
   if(GetForegroundWindow()!=first)ExitProcess(504504);
   ReleaseCapture();ExitProcess(0);
  }
 ExitProcess(504505);return 0;
}
void mainCRTStartup(void)
{
 struct Class c={0};c.proc=(U)procedure;c.instance=0x400000;c.name=(U)"CaptureThread";
 if(!RegisterClassA(&c))ExitProcess(504506);
 first=CreateWindowExA(0,"CaptureThread","foreground",0x90000000,10,10,150,80,0,0,0x400000,0);
 ShowWindow(first,5);SetForegroundWindow(first);
 if(!CreateThread(0,0,worker,0,0,0))ExitProcess(504507);
 struct Msg m;while(GetMessageA(&m,0,0,0)>0)DispatchMessageA(&m);
 ExitProcess(504508);
}
