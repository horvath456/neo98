/* SPDX-License-Identifier: GPL-2.0-or-later */
// A covered window that paints only when its paint DC has a visible clip.
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
typedef struct { U dc, erase; int left, top, right, bottom; U restore, incremental;
                 unsigned char reserved[32]; } PAINT;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U GetMessageA(MSG *,U,U,U);
API U DispatchMessageA(MSG *);
API U BeginPaint(U,PAINT *);
API U EndPaint(U,const PAINT *);
API U GetClientRect(U,int *);
API U GetClipBox(U,int *);
API U FillRect(U,const int *,U);
API U CreateSolidBrush(U);
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
static U background, red, blue;
static U __stdcall proc(U h,U m,U wp,U lp) {
    if(m==15) {
        PAINT p; int r[4]; U dc=BeginPaint(h,&p);
        if(GetClipBox(dc,r)>1) {
            GetClientRect(h,r); FillRect(dc,r,h==background?red:blue);
        }
        EndPaint(h,&p); return 0;
    }
    return DefWindowProcA(h,m,wp,lp);
}
void mainCRTStartup(void) {
    U instance=GetModuleHandleA(0);
    CLASS c={0,(U)proc,0,0,instance,0,0,0,0,"Exposure"};
    RegisterClassA(&c);
    red=CreateSolidBrush(0xff); blue=CreateSolidBrush(0xff0000);
    background=CreateWindowExA(0,"Exposure","Background",0x10cf0000,50,50,400,300,0,0,instance,0);
    CreateWindowExA(0,"Exposure","Cover",0x10cf0000,50,80,200,200,0,0,instance,0);
    MSG m;
    while(GetMessageA(&m,0,0,0)>0) DispatchMessageA(&m);
    ExitProcess(0);
}
