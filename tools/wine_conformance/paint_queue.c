/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND parent,child;
static unsigned recording,used;
static char events[2048];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording&&(m==WM_PAINT||m==WM_NCPAINT||m==WM_ERASEBKGND)&&used+32<sizeof events)
        used+=sprintf(events+used,"%c:%x:%u,",h==parent?'p':'c',m,GetUpdateRect(h,NULL,FALSE));
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void drain(HWND filter){MSG m;unsigned limit=0;while(limit++<32&&PeekMessageA(&m,filter,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}}
static void snapshot(unsigned kind,unsigned step,unsigned phase,BOOL found,const MSG*m)
{
    trace("PAINT_QUEUE clip=%u step=%u phase=%u found=%u target=%u update=%u,%u events=%s\n",kind,step,phase,
        found,found?(m->hwnd==parent?1:m->hwnd==child?2:3):0,GetUpdateRect(parent,NULL,FALSE),GetUpdateRect(child,NULL,FALSE),events);
    used=0;events[0]=0;fflush(stdout);
}
START_TEST(paint_queue)
{
    WNDCLASSA cls={0};unsigned kind,step;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="PaintQueue98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);ok(RegisterClassA(&cls)!=0,"class\n");
    for(kind=0;kind<2;++kind){
        parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE|(kind?WS_CLIPCHILDREN:0),100,100,220,220,NULL,NULL,cls.hInstance,NULL);
        child=CreateWindowA(cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE|WS_BORDER,10,10,100,100,parent,NULL,cls.hInstance,NULL);
        ok(parent&&child,"windows %u\n",kind);
        for(step=0;step<8;++step){
            MSG m={0},peek={0};BOOL found,again;RECT r={20,20,30,30};
            drain(NULL);RedrawWindow(parent,NULL,NULL,RDW_VALIDATE|RDW_NOFRAME|RDW_NOERASE|RDW_NOINTERNALPAINT|RDW_ALLCHILDREN);
            used=0;events[0]=0;recording=1;
            if(step==6)RedrawWindow(parent,&r,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
            else RedrawWindow(parent,NULL,NULL,RDW_INTERNALPAINT|
                ((step==1||step==3)?RDW_ALLCHILDREN:step==2?0:RDW_NOCHILDREN)|
                (step==4?RDW_UPDATENOW:0));
            if(step==3)RedrawWindow(parent,NULL,NULL,RDW_NOINTERNALPAINT|RDW_NOCHILDREN);
            if(step==5)UpdateWindow(parent);
            if(step==7)ValidateRect(parent,NULL);
            found=PeekMessageA(&peek,step==6?child:parent,WM_PAINT,WM_PAINT,PM_NOREMOVE);
            snapshot(kind,step,0,found,&peek);
            found=PeekMessageA(&m,parent,WM_PAINT,WM_PAINT,PM_REMOVE);
            snapshot(kind,step,1,found,&m);
            again=PeekMessageA(&peek,parent,WM_PAINT,WM_PAINT,PM_NOREMOVE);
            snapshot(kind,step,2,again,&peek);
            if(found)DispatchMessageA(&m);
            snapshot(kind,step,3,FALSE,&m);
            drain(parent);snapshot(kind,step,4,FALSE,&m);
            recording=0;
        }
        DestroyWindow(parent);parent=child=NULL;
    }
}
