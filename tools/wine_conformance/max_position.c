/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned kind,visible;
static POINT proposed;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_GETMINMAXINFO)proposed=((MINMAXINFO*)l)->ptMaxPosition;
    return DefWindowProcA(h,m,w,l);
}
static void state(HWND h,const char *step)
{
    RECT r;WINDOWPLACEMENT p={sizeof p};
    ok(GetWindowRect(h,&r) && GetWindowPlacement(h,&p),"placement\n");
    trace("MAX_POSITION kind=%u visible=%u step=%s rect=%ld,%ld,%ld,%ld saved=%ld,%ld proposed=%ld,%ld command=%u\n",kind,visible,step,r.left,r.top,r.right,r.bottom,p.ptMaxPosition.x,p.ptMaxPosition.y,proposed.x,proposed.y,p.showCmd);
}
START_TEST(max_position)
{
    WNDCLASSA cls={0};HWND parent,h;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MaxPosition98";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,500,400,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"parent\n");
    for(kind=0;kind<3;++kind)for(visible=0;visible<2;++visible){
        DWORD style=kind==0?WS_OVERLAPPEDWINDOW:kind==1?WS_POPUP:WS_CHILD;
        h=CreateWindowA(cls.lpszClassName,"maximum",style|WS_MAXIMIZE|(visible?WS_VISIBLE:0),40,40,200,150,kind==2?parent:NULL,NULL,cls.hInstance,NULL);
        ok(h!=NULL,"window\n");state(h,"create");
        SetWindowPos(h,NULL,10,10,200,200,SWP_NOZORDER|SWP_NOACTIVATE);state(h,"move");
        ShowWindow(h,SW_HIDE);state(h,"hide");
        ShowWindow(h,SW_SHOWMAXIMIZED);state(h,"show-max");
        ShowWindow(h,SW_RESTORE);state(h,"restore");
        ShowWindow(h,SW_SHOWMAXIMIZED);state(h,"maximize");
        DestroyWindow(h);
    }
    DestroyWindow(parent);
}
