/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND parent,child;
static unsigned recording,used;
static char events[2048];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording&&(m==WM_PAINT||m==WM_NCPAINT||m==WM_ERASEBKGND||m==WM_NCCALCSIZE||
        m==WM_WINDOWPOSCHANGING||m==WM_WINDOWPOSCHANGED)&&used+24<sizeof events)
        used+=sprintf(events+used,"%c:%x,",h==parent?'p':'c',m);
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void drain(void){MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}}
START_TEST(frame_paint)
{
    WNDCLASSA cls={0};unsigned clip,defer,dirty,phase;
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="FramePaint98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);ok(RegisterClassA(&cls)!=0,"class\n");
    for(clip=0;clip<2;++clip){
        parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE|(clip?WS_CLIPCHILDREN:0),100,100,220,220,NULL,NULL,cls.hInstance,NULL);
        child=CreateWindowA(cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE|WS_BORDER,10,10,100,100,parent,NULL,cls.hInstance,NULL);
        ok(parent&&child,"windows %u\n",clip);
        for(defer=0;defer<2;++defer)for(dirty=0;dirty<2;++dirty){
            BOOL result;drain();
            if(dirty)RedrawWindow(child,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);
            recording=1;used=0;events[0]=0;
            result=SetWindowPos(parent,NULL,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER|SWP_FRAMECHANGED|(defer?SWP_DEFERERASE:0));
            for(phase=0;phase<2;++phase){
                if(phase){used=0;events[0]=0;drain();}
                trace("FRAME_PAINT clip=%u defer=%u dirty=%u phase=%u result=%u update=%u,%u events=%s\n",
                    clip,defer,dirty,phase,result,GetUpdateRect(parent,NULL,FALSE),GetUpdateRect(child,NULL,FALSE),events);fflush(stdout);
            }
            recording=0;
        }
        DestroyWindow(parent);parent=child=NULL;
    }
}
