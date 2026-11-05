// SPDX-License-Identifier: GPL-2.0-or-later
// Like Wonderland Adventures after it opened the browser: a thread in
// the background keeps the mouse capture it took on a press, while another
// thread owns the foreground window. A click on the foreground window belongs
// to the foreground thread; only a click on the captured thread's own window
// goes to its capture.
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
static volatile U first,second,first_down,first_up;
static U S procedure(U h,U m,U w,U l)
{
 if(h==first&&m==0x201)first_down=1;
 if(h==first&&m==0x202){if(!first_down)ExitProcess(537401);first_up=1;}
 if(h==second&&(m==0x201||m==0x202)&&!first_up)ExitProcess(537402); // the first click was taken by the capture
 if(h==second&&m==0x202){if(GetCapture()!=second)ExitProcess(537403);ReleaseCapture();ExitProcess(0);}
 return DefWindowProcA(h,m,w,l);
}
static U S worker(void *unused)
{
 (void)unused;
 second=CreateWindowExA(0,"CaptureQueue","captured",0x90000000,250,200,150,80,0,0,0x400000,0);
 ShowWindow(second,4); // SW_SHOWNOACTIVATE, the foreground stays on thread 1.
 if(GetForegroundWindow()!=first)ExitProcess(537404);
 SetCapture(second);
 if(GetCapture()!=second)ExitProcess(537405);
 U n,file=CreateFileA("C:\\WINDOWS\\TEMP\\CAPTURE-QUEUE.DAT",0x40000000,3,0,2,0,0);
 U both[2]={first,second};
 if(file==~0u||!WriteFile(file,both,8,&n,0)||n!=8||!CloseHandle(file))ExitProcess(537406);
 struct Msg m;while(GetMessageA(&m,0,0,0)>0)DispatchMessageA(&m);
 ExitProcess(537407);return 0;
}
void mainCRTStartup(void)
{
 struct Class c={0};c.proc=(U)procedure;c.instance=0x400000;c.name=(U)"CaptureQueue";
 if(!RegisterClassA(&c))ExitProcess(537408);
 first=CreateWindowExA(0,"CaptureQueue","foreground",0x90000000,10,10,150,80,0,0,0x400000,0);
 ShowWindow(first,5);SetForegroundWindow(first);
 if(!CreateThread(0,0,worker,0,0,0))ExitProcess(537409);
 struct Msg m;while(GetMessageA(&m,0,0,0)>0)DispatchMessageA(&m);
 ExitProcess(537410);
}
