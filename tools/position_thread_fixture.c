// SPDX-License-Identifier: GPL-2.0-or-later
// Two window threads move each other's windows. SetWindowPos and ShowWindow
// must dispatch positioning messages on the target window's owning thread
// through SendMessage.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
struct Msg{U hwnd,message,wp,lp,time;int x,y;};
struct WndClass{U style,proc,cbcls,cbwin,instance,icon,cursor,brush,menu,name;};
struct Rect{int left,top,right,bottom;};
struct WindowPos{U hwnd,after;int x,y,cx,cy;U flags;};
API void S ExitProcess(U);API void S Sleep(U);
API U S GetCurrentThreadId(void);
API U S CreateEventA(void*,U,U,const char*);API U S WaitForSingleObject(U,U);API U S SetEvent(U);
API U S CreateThread(void*,U,U(S*)(void*),void*,U,U*);API U S CloseHandle(U);
API U S RegisterClassA(struct WndClass*);
API U S CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,void*);
API U S DestroyWindow(U);API U S DefWindowProcA(U,U,U,U);
API U S GetMessageA(struct Msg*,U,U,U);API U S PeekMessageA(struct Msg*,U,U,U,U);
API U S DispatchMessageA(struct Msg*);API void S PostQuitMessage(U);
API U S PostMessageA(U,U,U,U);API U S ShowWindow(U,int);
API U S SetWindowPos(U,U,int,int,int,int,U);API U S GetWindowRect(U,struct Rect*);

#define CLASS "Neo98PositionThread"
#define WM_STEP 0x0401
// Messages whose executing thread the test records.
#define POSCHANGING 0x46
#define NCCALCSIZE  0x83
#define POSCHANGED  0x47
#define SHOWWINDOW  0x18
#define SIZE        0x05
#define MOVE        0x03

static volatile U main_window,worker_window,main_thread,worker_thread,failed;
// Record the executing thread for each window/message pair.
static volatile U on_main[6],on_worker[6],clamped,ready;
static U release_event;

static int slot(U m){
 if(m==POSCHANGING)return 0;
 if(m==NCCALCSIZE)return 1;
 if(m==POSCHANGED)return 2;
 if(m==SHOWWINDOW)return 3;
 if(m==SIZE)return 4;
 if(m==MOVE)return 5;
 return -1;
}
static U S wndproc(U h,U m,U w,U l){
 const int s=slot(m);
 if(s>=0){
  if(h==main_window)on_main[s]=GetCurrentThreadId();
  else if(h==worker_window)on_worker[s]=GetCurrentThreadId();
 }
 // The receiver may change WINDOWPOS. The caller on another thread must see
 // those changes, testing the reply path.
 if(h==main_window&&m==POSCHANGING&&clamped&&l){
  struct WindowPos*p=(struct WindowPos*)l;
  if(p->cx>180)p->cx=180;
 }
 if(h==main_window&&m==WM_STEP){
  // Step two runs from the main thread in the opposite direction. The worker
  // receives the send through GetMessage.
  if(!SetWindowPos(worker_window,0,260,210,150,90,0x14))failed=29791;
  ready=1;
  PostQuitMessage(0);
  return 0;
 }
 return DefWindowProcA(h,m,w,l);
}
// Worker thread: create a window, then change the window of the
// main thread.
static U S worker(void*p){
 (void)p;
 worker_thread=GetCurrentThreadId();
 worker_window=CreateWindowExA(0,CLASS,"worker",0x90000000,300,60,120,60,0,0,0x400000,0);
 if(!worker_window)failed=29801;
 if(worker_window){
  ShowWindow(worker_window,5);
  // Step one moves and resizes the main-thread window. Its positioning, move
  // and size messages must execute on the main thread.
  clamped=1;
  struct Rect r={0,0,0,0};
  if(!SetWindowPos(main_window,0,40,50,240,170,0x14))failed=29802;
  else if(!GetWindowRect(main_window,&r))failed=29803;
  else if(r.right-r.left!=180)failed=29804;
  else if(r.left!=40||r.top!=50)failed=29805;
  clamped=0;
  // Reproduce Super Collapse: a loading thread shows a window owned by the
  // main thread.
  ShowWindow(main_window,0);
  ShowWindow(main_window,5);
 }
 // Always notify the main thread, including after a failure, so it cannot
 // remain blocked in GetMessage.
 if(!PostMessageA(main_window,WM_STEP,0,0))failed=29806;
 // Pump from here to receive step two.
 struct Msg m;
 while(!ready){
  while(PeekMessageA(&m,0,0,0,1))DispatchMessageA(&m);
  Sleep(1);
 }
 while(PeekMessageA(&m,0,0,0,1))DispatchMessageA(&m);
 WaitForSingleObject(release_event,60000);
 return 0;
}
void mainCRTStartup(void){
 struct WndClass c={0};c.proc=(U)wndproc;c.instance=0x400000;c.name=(U)CLASS;
 main_thread=GetCurrentThreadId();
 if(!RegisterClassA(&c))ExitProcess(29761);
 main_window=CreateWindowExA(0,CLASS,"main",0x90000000,10,10,150,80,0,0,0x400000,0);
 if(!main_window)ExitProcess(29762);
 ShowWindow(main_window,5);
 release_event=CreateEventA(0,1,0,0);
 if(!release_event)ExitProcess(29763);
 if(!CreateThread(0,0,worker,0,0,0))ExitProcess(29764);
 struct Msg m;
 while(GetMessageA(&m,0,0,0))DispatchMessageA(&m);
 SetEvent(release_event);
 if(failed)ExitProcess(failed);
 if(main_thread==worker_thread)ExitProcess(29765);
 // The worker changes the main thread's window.
 if(on_main[0]!=main_thread)ExitProcess(29770);
 if(on_main[1]!=main_thread)ExitProcess(29771);
 if(on_main[2]!=main_thread)ExitProcess(29772);
 if(on_main[3]!=main_thread)ExitProcess(29773);
 if(on_main[4]!=main_thread)ExitProcess(29774);
 if(on_main[5]!=main_thread)ExitProcess(29775);
 // The main thread changes the worker's window.
 if(on_worker[0]!=worker_thread)ExitProcess(29780);
 if(on_worker[1]!=worker_thread)ExitProcess(29781);
 if(on_worker[2]!=worker_thread)ExitProcess(29782);
 if(on_worker[4]!=worker_thread)ExitProcess(29784);
 if(on_worker[5]!=worker_thread)ExitProcess(29785);
 ExitProcess(0);
}
