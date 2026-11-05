/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static void pump(void)
{
    MSG msg;DWORD start=GetTickCount();
    do {
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
        Sleep(10);
    }while(GetTickCount()-start<100);
}
START_TEST(edit_input)
{
    WNDCLASSA cls={0};HWND parent,edit;POINT saved;unsigned multiline,step;
    cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="EditInputParent";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        50,50,320,240,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"parent\n");GetCursorPos(&saved);SetForegroundWindow(parent);pump();
    for(multiline=0;multiline<2;++multiline) {
        POINT point={12,4};
        edit=CreateWindowA("EDIT","alpha beta",WS_CHILD|WS_VISIBLE|(multiline?ES_MULTILINE:0),
            10,10,160,40,parent,(HMENU)10,cls.hInstance,NULL);
        ok(edit!=NULL,"edit %u\n",multiline);
        SendMessageA(edit,WM_SETFONT,(WPARAM)GetStockObject(SYSTEM_FIXED_FONT),TRUE);
        UpdateWindow(edit);SetFocus(NULL);SendMessageA(edit,EM_SETSEL,0,0);
        ClientToScreen(edit,&point);SetCursorPos(point.x,point.y);pump();
        for(step=0;step<3;++step) {
            DWORD first,last;
            if(step==0)mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,0);
            if(step==1)SetCursorPos(point.x+23,point.y);
            if(step==2)mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,0);
            pump();SendMessageA(edit,EM_GETSEL,(WPARAM)&first,(LPARAM)&last);
            trace("EDIT_INPUT multiline=%u step=%u focus=%u capture=%u sel=%lu,%lu\n",
                multiline,step,GetFocus()==edit,GetCapture()==edit,first,last);
        }
        DestroyWindow(edit);pump();
    }
    DestroyWindow(parent);SetCursorPos(saved.x,saved.y);pump();
}
