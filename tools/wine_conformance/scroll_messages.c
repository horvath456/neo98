/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND parent,child;
static unsigned recording,used;
static char events[2048];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(recording&&(m==WM_PAINT||m==WM_ERASEBKGND||m==WM_NCPAINT||m==WM_MOVE||
        m==WM_WINDOWPOSCHANGING||m==WM_WINDOWPOSCHANGED)&&used+24<sizeof events)
        used+=sprintf(events+used,"%c:%x,",h==parent?'p':'c',m);
    if(m==WM_PAINT){PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void drain(void){MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}}
START_TEST(scroll_messages)
{
    WNDCLASSA cls={0};unsigned rect,step,phase,vector,output;const unsigned flags[]={1,3,7,6};
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ScrollMessages98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,200,200,NULL,NULL,cls.hInstance,NULL);
    child=CreateWindowA(cls.lpszClassName,"child",WS_CHILD|WS_VISIBLE|WS_CAPTION,10,10,150,150,parent,NULL,cls.hInstance,NULL);
    ok(parent&&child,"windows\n");
    for(vector=0;vector<4;++vector)for(output=0;output<4;++output)for(rect=0;rect<2;++rect)for(step=0;step<5;++step){
        RECT r={0,0,130,130},bounds,update={0};POINT at;LRESULT result;
        int dx=vector==0?0:vector==3?300:10,dy=vector==0||vector==2?0:vector==3?300:10;
        HRGN region=(output==1||output==3)?CreateRectRgn(0,0,0,0):NULL;
        unsigned region_kind=0;RECT region_box;
        SetWindowPos(child,NULL,10,10,150,150,SWP_NOZORDER|SWP_NOACTIVATE);
        RedrawWindow(parent,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_ALLCHILDREN);drain();
        recording=1;used=0;events[0]=0;
        result=step==4?ScrollWindow(parent,dx,dy,rect?&r:NULL,NULL):
            ScrollWindowEx(parent,dx,dy,rect?&r:NULL,NULL,region,(output==1||output==2)?&update:NULL,flags[step]);
        if(region)region_kind=GetRgnBox(region,&region_box);
        for(phase=0;phase<2;++phase){
            if(phase){used=0;events[0]=0;drain();}
            GetWindowRect(child,&bounds);at.x=bounds.left;at.y=bounds.top;ScreenToClient(parent,&at);
            trace("SCROLL_MESSAGES vector=%u output=%u rect=%u step=%u phase=%u result=%ld child=%ld,%ld region=%u update=%ld,%ld,%ld,%ld events=%s\n",
                vector,output,rect,step,phase,result,at.x,at.y,region_kind,update.left,update.top,update.right,update.bottom,events);fflush(stdout);
        }
        recording=0;if(region)DeleteObject(region);
    }
    DestroyWindow(parent);
}
