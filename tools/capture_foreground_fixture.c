// SPDX-License-Identifier: GPL-2.0-or-later
// The capture of the foreground queue, with per-queue ownership. Thread 1 owns the
// foreground window and holds the mouse with no button down; a click on the
// window of thread 2 belongs to thread 2 (USER hands it to the queue
// of the window under the pointer), and USER posts the capture a press and
// a release of its own at the point of the click. Then a drag
// that starts on the foreground window, which takes the capture on the press,
// and ends over the other thread's window still goes to the capture: the
// first press fixed the queue (ds:154h).
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
API U S GetMessageA(struct Msg*,U,U,U);API U S DispatchMessageA(struct Msg*);
static volatile U first,second,phase,first_down,first_up,second_move,second_down,second_up,dragged;
static void write(const char *name,U a,U b)
{
 U n,both[2]={a,b},file=CreateFileA(name,0x40000000,3,0,2,0,0);
 if(file==~0u||!WriteFile(file,both,8,&n,0)||n!=8||!CloseHandle(file))ExitProcess(537420);
}
// Both threads saw their part of the click: the drag starts.
static void clicked(void)
{
 if(phase==1&&first_up&&second_up){phase=2;write("C:\\WINDOWS\\TEMP\\CAPTURE-DRAG.DAT",first,second);}
}
static U S procedure(U h,U m,U w,U l)
{
 const int x=(short)(l&65535);
 const U now=phase; // a message of the click must not count for the drag
 if(now==1&&h==first)
 {
  // The press and release USER posts to the capture, at the point of the
  // click on the other thread's window (right of this one).
  if(m==0x200&&x>=150)ExitProcess(537421); // the move over the other window went to the capture
  if(m==0x201){if(x<150||GetCapture()!=first)ExitProcess(537422);first_down=1;}
  if(m==0x202){if(!first_down||x<150)ExitProcess(537423);first_up=1;ReleaseCapture();clicked();}
 }
 if(now==1&&h==second)
 {
  if(m==0x200)second_move=1;
  if(m==0x201){if(!second_move)ExitProcess(537424);second_down=1;}
  if(m==0x202){if(!second_down)ExitProcess(537425);second_up=1;clicked();}
 }
 if(now==2&&h==first)
 {
  if(m==0x201){SetCapture(first);if(GetCapture()!=first)ExitProcess(537426);}
  if(m==0x200&&(w&1)&&x>=150&&GetCapture()==first)dragged=1;
  if(m==0x202){if(!dragged)ExitProcess(537427);if(GetCapture()!=first)ExitProcess(537428);ReleaseCapture();ExitProcess(0);}
 }
 // The drag over the other thread's window belongs to the capture. A move
 // without a button may come when the capture goes (USER refreshes the
 // pointer after ReleaseCapture).
 if(now==2&&h==second&&(m==0x201||m==0x202||(m==0x200&&(w&1))))ExitProcess(537429);
 return DefWindowProcA(h,m,w,l);
}
static U S worker(void *unused)
{
 (void)unused;
 second=CreateWindowExA(0,"CaptureForeground","other",0x90000000,250,200,150,80,0,0,0x400000,0);
 ShowWindow(second,4); // SW_SHOWNOACTIVATE, the foreground stays on thread 1.
 if(GetForegroundWindow()!=first)ExitProcess(537430);
 phase=1;
 write("C:\\WINDOWS\\TEMP\\CAPTURE-FOREGROUND.DAT",first,second);
 struct Msg m;while(GetMessageA(&m,0,0,0)>0)DispatchMessageA(&m);
 ExitProcess(537431);return 0;
}
void mainCRTStartup(void)
{
 struct Class c={0};c.proc=(U)procedure;c.instance=0x400000;c.name=(U)"CaptureForeground";
 if(!RegisterClassA(&c))ExitProcess(537432);
 first=CreateWindowExA(0,"CaptureForeground","foreground",0x90000000,10,10,150,80,0,0,0x400000,0);
 ShowWindow(first,5);SetForegroundWindow(first);
 // The foreground queue holds the mouse, no button down.
 SetCapture(first);
 if(GetCapture()!=first)ExitProcess(537433);
 if(!CreateThread(0,0,worker,0,0,0))ExitProcess(537434);
 struct Msg m;while(GetMessageA(&m,0,0,0)>0)DispatchMessageA(&m);
 ExitProcess(537435);
}
