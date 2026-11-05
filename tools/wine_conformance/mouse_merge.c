/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static void flush(void)
{
    MSG msg; DWORD start=GetTickCount();
    do {
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
        Sleep(10);
    } while(GetTickCount()-start<100);
}
START_TEST(mouse_merge)
{
    WNDCLASSA cls={0}; HWND window; MSG msg={0}; POINT saved;
    unsigned i; DWORD previous=0;
    cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MouseMerge";
    ok(RegisterClassA(&cls)!=0,"register window class\n");
    window=CreateWindowA(cls.lpszClassName,"Mouse merge",WS_POPUP|WS_VISIBLE,50,50,500,400,NULL,NULL,cls.hInstance,NULL);
    ok(window!=NULL,"create visible window\n");if(!window)return;
    GetCursorPos(&saved);SetForegroundWindow(window);SetCapture(window);SetCursorPos(100,100);flush();
    for(i=0;i<3;++i){
        POINT cursor;BOOL found;
        Sleep(40);SetCursorPos(150+20*i,150+20*i);GetCursorPos(&cursor);
        found=PeekMessageA(&msg,window,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_NOREMOVE);
        trace("MERGE_MOVE step=%u found=%u latest=%u advanced=%u\n",i,!!found,
              found&&msg.pt.x==cursor.x&&msg.pt.y==cursor.y,found&&(!i||msg.time>previous));
        previous=msg.time;
    }
    trace("MERGE_REMOVE found=%u\n",!!PeekMessageA(&msg,window,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE));
    trace("MERGE_EMPTY found=%u\n",!!PeekMessageA(&msg,window,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE));
    flush();
    ok(PostMessageA(window,WM_MOUSEMOVE,0,0x0014000a),"post first synthetic move\n");
    ok(PostMessageA(window,WM_MOUSEMOVE,0,0x0028001e),"post second synthetic move\n");
    for(i=0;i<2;++i){
        BOOL found=PeekMessageA(&msg,window,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE);
        trace("MERGE_POSTED step=%u found=%u value=%08lx\n",i,!!found,msg.lParam);
    }
    trace("MERGE_POSTED_EMPTY found=%u\n",!!PeekMessageA(&msg,window,WM_MOUSEMOVE,WM_MOUSEMOVE,PM_REMOVE));
    ReleaseCapture();DestroyWindow(window);SetCursorPos(saved.x,saved.y);flush();
}
