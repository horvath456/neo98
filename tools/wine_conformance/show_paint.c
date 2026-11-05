/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND observed;
static unsigned count;
static char events[1024];
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(h==observed && (m==WM_PAINT || m==WM_NCPAINT || m==WM_ERASEBKGND ||
        m==WM_WINDOWPOSCHANGING || m==WM_WINDOWPOSCHANGED || m==WM_NCCALCSIZE)){
        if(count<50)sprintf(events+strlen(events),"%x,",m);
        ++count;
    }
    if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
    return DefWindowProcA(h,m,w,l);
}
static void drain(void){MSG m;while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageA(&m);}}
START_TEST(show_paint)
{
    WNDCLASSA cls={0};unsigned kind,command,phase;
    const DWORD styles[]={WS_POPUP,WS_POPUP|WS_BORDER,WS_POPUP|WS_CAPTION,WS_OVERLAPPEDWINDOW};
    const unsigned commands[]={SW_SHOW,SW_SHOWNA,SW_SHOWMAXIMIZED,SW_SHOWNOACTIVATE};
    cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ShowPaint98";
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    ok(RegisterClassA(&cls)!=0,"class\n");
    for(kind=0;kind<4;++kind)for(command=0;command<4;++command){
        HWND h=CreateWindowA(cls.lpszClassName,"paint",styles[kind],40,40,200,150,NULL,NULL,cls.hInstance,NULL);
        ok(h!=NULL,"window\n");drain();observed=h;count=0;events[0]=0;
        ShowWindow(h,commands[command]);
        for(phase=0;phase<2;++phase){
            if(phase){count=0;events[0]=0;drain();}
            ok(count<=50,"complete messages\n");
            trace("SHOW_PAINT kind=%u command=%u phase=%u update=%u count=%u events=%s\n",kind,commands[command],phase,GetUpdateRect(h,NULL,FALSE),count,events);
        }
        observed=NULL;DestroyWindow(h);drain();
    }
}
