/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND parent,child;
static unsigned recording,used;
static char events[4096];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording&&(m==WM_PAINT||m==WM_NCPAINT||m==WM_ERASEBKGND)&&used+24<sizeof events)
        used+=sprintf(events+used,"%c:%x,",h==parent?'p':'c',m);
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void drain(void)
{
    MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}
}
START_TEST(redraw_messages)
{
    WNDCLASSA cls={0};unsigned kind,step;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="RedrawMessages98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    ok(RegisterClassA(&cls)!=0,"class\n");
    for(kind=0;kind<3;++kind){
        parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE|
            (kind==2?WS_CLIPCHILDREN:0),100,100,220,220,NULL,NULL,cls.hInstance,NULL);
        ok(parent!=NULL,"parent %u\n",kind);
        child=kind?CreateWindowA(cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE|WS_BORDER,
            10,10,100,100,parent,NULL,cls.hInstance,NULL):NULL;
        if(kind)ok(child!=NULL,"child %u\n",kind);
        for(step=0;step<10;++step){
            RECT r={0,0,50,50},p={0},c={0};BOOL pu,cu=FALSE;
            drain();RedrawWindow(parent,NULL,NULL,RDW_VALIDATE|RDW_NOFRAME|RDW_NOERASE|RDW_ALLCHILDREN);
            used=0;events[0]=0;recording=1;
            if(step<4)RedrawWindow(parent,step&1?&r:NULL,NULL,RDW_INVALIDATE|RDW_FRAME|RDW_ERASE|
                (step<2?RDW_UPDATENOW:RDW_ERASENOW));
            else if(step<8){
                RedrawWindow(parent,&r,NULL,RDW_INVALIDATE|RDW_ERASE|((step&1)?0:RDW_FRAME));
                RedrawWindow(parent,NULL,NULL,RDW_ERASENOW|(step<6?RDW_ALLCHILDREN:0));
                if(child)RedrawWindow(child,NULL,NULL,RDW_ERASENOW);
            }else{
                RedrawWindow(parent,&r,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
                RedrawWindow(child?child:parent,NULL,NULL,step==8?RDW_UPDATENOW:RDW_ERASENOW);
            }
            pu=GetUpdateRect(parent,&p,FALSE);if(child)cu=GetUpdateRect(child,&c,FALSE);
            recording=0;
            trace("REDRAW_MESSAGES kind=%u step=%u parent=%u:%ld,%ld,%ld,%ld child=%u:%ld,%ld,%ld,%ld events=%s\n",
                kind,step,pu,p.left,p.top,p.right,p.bottom,cu,c.left,c.top,c.right,c.bottom,events);
            fflush(stdout);
        }
        DestroyWindow(parent);parent=child=NULL;
    }
}
