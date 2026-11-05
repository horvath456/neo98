/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U ShowWindow(U,int);
API U InvalidateRect(U,const void *,int);
API U UpdateWindow(U);
API U BeginPaint(U,void *);
API U EndPaint(U,void *);
API U GetDC(U);
API U ReleaseDC(U,U);
API U FillRect(U,const void *,U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U GetPixel(U,int,int);
static U color=0x0000ff,paints;
static U __stdcall proc(U h,U m,U w,U l){
    if(m==0xf){U ps[16]={0};U dc=BeginPaint(h,ps);int r[4]={0,0,150,100};
        U brush=CreateSolidBrush(color);FillRect(dc,r,brush);DeleteObject(brush);EndPaint(h,ps);++paints;return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void check(int ok,U code){if(!ok)ExitProcess(code);}
void mainCRTStartup(void){
    struct {U style,proc,a,b,instance,icon,cursor,brush;const char *menu,*name;} cls={0,(U)proc,0,0,0,0,0,0,0,"HiddenPaint"};
    check(RegisterClassA(&cls)!=0,1);
    U parent=CreateWindowExA(0,cls.name,"parent",0x90000000,100,100,150,100,0,0,0,0);
    U child=CreateWindowExA(0,cls.name,"child",0x50000000,20,20,40,30,parent,0,0,0);
    check(parent&&child,2);UpdateWindow(parent);
    InvalidateRect(child,0,0);ShowWindow(child,0);
    color=0x00ff00;InvalidateRect(parent,0,0);
    /* Win98 may send WM_PAINT here, but the invisible child must not
       validate its rectangle in the parent's pending update region. */
    UpdateWindow(child);UpdateWindow(parent);
    U dc=GetDC(parent);check(dc!=0&&paints!=0,3);
    check(GetPixel(dc,5,5)==color,4);
    check(GetPixel(dc,25,25)==color,5);
    ReleaseDC(parent,dc);ExitProcess(0);
}
