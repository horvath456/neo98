/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND observed,top,other;
static unsigned count;
static char sequence[512];
static unsigned role(HWND h)
{
    if(!h)return 0;
    if(h==observed)return 1;
    if(h==top)return 2;
    if(h==other)return 3;
    return (GetWindowLongA(h,GWL_EXSTYLE)&WS_EX_TOPMOST)?4:5;
}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(h==observed && (m==WM_WINDOWPOSCHANGING || m==WM_WINDOWPOSCHANGED)){
        WINDOWPOS *p=(WINDOWPOS*)l;
        if(count<12)sprintf(sequence+strlen(sequence),"%x:%x:%u,",m,p->flags,role(p->hwndInsertAfter));
        ++count;
    }
    return DefWindowProcA(h,m,w,l);
}
START_TEST(show_order)
{
    WNDCLASSA cls={0};unsigned mode,round,first,previous;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ShowOrder98";
    ok(RegisterClassA(&cls)!=0,"class\n");
    top=CreateWindowExA(WS_EX_TOPMOST,cls.lpszClassName,"top",WS_POPUP|WS_VISIBLE,0,0,20,20,NULL,NULL,cls.hInstance,NULL);
    other=CreateWindowA(cls.lpszClassName,"other",WS_OVERLAPPEDWINDOW|WS_VISIBLE,200,200,150,100,NULL,NULL,cls.hInstance,NULL);
    ok(top!=NULL && other!=NULL,"fixtures\n");
    for(mode=0;mode<4;++mode){
        HWND h=CreateWindowA(cls.lpszClassName,"observed",WS_OVERLAPPEDWINDOW,40,40,150,100,NULL,NULL,cls.hInstance,NULL);
        ok(h!=NULL,"observed %u\n",mode);observed=h;
        if(mode&1)SetForegroundWindow(other);
        else SetForegroundWindow(h);
        if(mode&2)SetWindowPos(h,other,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        for(round=0;round<3;++round){
            first=role(GetTopWindow(NULL));previous=role(GetWindow(h,GW_HWNDPREV));
            count=0;sequence[0]=0;
            ShowWindow(h,SW_SHOWNA);
            ok(count<=12,"complete messages\n");
            trace("SHOW_ORDER mode=%u round=%u first=%u previous=%u active=%u after_first=%u after_previous=%u count=%u sequence=%s\n",mode,round,first,previous,role(GetActiveWindow()),role(GetTopWindow(NULL)),role(GetWindow(h,GW_HWNDPREV)),count,sequence);
        }
        observed=NULL;DestroyWindow(h);
    }
    DestroyWindow(other);DestroyWindow(top);
}
