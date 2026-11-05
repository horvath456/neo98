// SPDX-License-Identifier: GPL-2.0-or-later
// Two window threads: one waits on an event without pumping; the other waits
// in GetMessage. User input must reach the pumping thread even while the
// remembered input thread is blocked.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg{U hwnd,message,wp,lp,time;int x,y;};
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
API void S ExitProcess(U);API void S Sleep(U);
API U S CreateEventA(void*,U,U,const char*);API U S WaitForSingleObject(U,U);API U S SetEvent(U);
API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);API U S CloseHandle(U);
API U S CreateFileA(const char*,U,U,U,U,U,U);API U S WriteFile(U,const void*,U,U*,U);
API U S RegisterClassA(struct WndClass*);API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DestroyWindow(U);API U S DefWindowProcA(U,U,U,U);API U S GetMessageA(struct Msg*,U,U,U);
API U S DispatchMessageA(struct Msg*);API void S PostQuitMessage(U);API U S ShowWindow(U,int);
API U S SetForegroundWindow(U);API U S GetForegroundWindow(void);API U S PostMessageA(U,U,U,U);

#define CLASS "Neo98InputThread"
static volatile U main_window,worker_window,clicks,keys,failed;
static U release_event;

static U S wndproc(U h,U m,U w,U l){
 if(h==main_window){
  if(m==0x201)++clicks;                  // WM_LBUTTONDOWN
  if(m==0x100)++keys;                    // WM_KEYDOWN
  // The click ends the run. Keyboard input follows focus; this test does not
  // assert which window activation focuses.
  if(clicks)PostQuitMessage(0);
 }
 if(h==worker_window&&(m==0x201||m==0x100))failed=29760; // The blocked thread must not dispatch input.
 return DefWindowProcA(h,m,w,l);
}
// The foreground-window owner waits on an event, exercising input routing
// while that thread cannot pump.
static U S worker(void*p){
 (void)p;
 worker_window=CreateWindowExA(0,CLASS,"worker",0x90000000,200,200,120,60,0,0,0x400000,0);
 if(!worker_window){failed=29761;return 1;}
 ShowWindow(worker_window,5);
 SetForegroundWindow(worker_window);
 // Write the ready file on this thread before the runner sends input. The
 // main thread must keep pumping because showing the window sends it a
 // synchronous message.
 U n,h=CreateFileA("C:\\WINDOWS\\TEMP\\INPUT-THREAD.DAT",0x40000000,3,0,2,0,0);
 U value=main_window;
 if(h==~0u||!WriteFile(h,&value,4,&n,0)||n!=4||!CloseHandle(h)){failed=29767;return 1;}
 WaitForSingleObject(release_event,60000);
 return 0;
}
void mainCRTStartup(void){
 struct WndClass c={0};c.proc=(U)wndproc;c.instance=0x400000;c.name=(U)CLASS;
 if(!RegisterClassA(&c))ExitProcess(29762);
 main_window=CreateWindowExA(0,CLASS,"main",0x90000000,10,10,150,80,0,0,0x400000,0);
 if(!main_window)ExitProcess(29763);
 ShowWindow(main_window,5);
 release_event=CreateEventA(0,1,0,0);
 if(!release_event)ExitProcess(29764);
 if(!CreateThread(0,0,worker,0,0,0))ExitProcess(29765);
 struct Msg m;
 while(GetMessageA(&m,0,0,0))DispatchMessageA(&m);
 SetEvent(release_event);
 if(failed)ExitProcess(failed);
 if(!clicks)ExitProcess(29768);
 ExitProcess(0);
}
