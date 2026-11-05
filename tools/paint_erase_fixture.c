/* SPDX-License-Identifier: GPL-2.0-or-later
 * fErase survives a paint whose WM_ERASEBKGND returned zero. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U dc, erase; int rect[4]; U restore, incremental; char reserved[32]; } Paint;
typedef struct { U style; U (__stdcall *proc)(U,U,U,U); int ce,we;
    U instance,icon,cursor,background; const char *menu,*name; } Class;
API void ExitProcess(U);
API U RegisterClassA(const Class *);
API U CreateWindowExA(U,const char*,const char*,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U InvalidateRect(U,const void*,U);
API U UpdateWindow(U);
API U BeginPaint(U,Paint*);
API U EndPaint(U,const Paint*);
API U GetUpdateRect(U,void*,U);
API U DestroyWindow(U);
static U handled=1, erases, expected, paints, phase;
static U __stdcall procedure(U h,U m,U w,U l) {
    if(m==0x14) { ++erases; return handled; }
    if(m==0xf) {
        Paint p;
        if(!BeginPaint(h,&p) || p.erase!=expected) ExitProcess(100+phase);
        ++paints;
        EndPaint(h,&p);
        return 0;
    }
    return DefWindowProcA(h,m,w,l);
}
void mainCRTStartup(void) {
    Class c={0,procedure,0,0,0,0,0,0,0,"ErasePersistence"};
    if(!RegisterClassA(&c)) ExitProcess(1);
    U h=CreateWindowExA(0,c.name,"Erase persistence",0x10000000,20,20,200,100,0,0,0,0);
    if(!h || !UpdateWindow(h)) ExitProcess(2);
    erases=0;
    phase=1; handled=0; expected=1;
    InvalidateRect(h,0,1); UpdateWindow(h);
    if(erases!=1) ExitProcess(11);
    phase=2;
    InvalidateRect(h,0,0); UpdateWindow(h);
    if(erases!=1) ExitProcess(12);
    Paint p;
    if(!BeginPaint(h,&p) || p.erase!=1) ExitProcess(13);
    EndPaint(h,&p);
    phase=3; handled=1; expected=0;
    InvalidateRect(h,0,1); UpdateWindow(h);
    if(erases!=2) ExitProcess(14);
    phase=4;
    InvalidateRect(h,0,0); UpdateWindow(h);
    if(erases!=2) ExitProcess(15);
    phase=5; handled=0; expected=1;
    InvalidateRect(h,0,1); GetUpdateRect(h,0,1); UpdateWindow(h);
    if(erases!=3) ExitProcess(16);
    phase=6;
    InvalidateRect(h,0,0); UpdateWindow(h);
    if(erases!=3 || paints<7) ExitProcess(17);
    DestroyWindow(h);
    ExitProcess(0);
}
