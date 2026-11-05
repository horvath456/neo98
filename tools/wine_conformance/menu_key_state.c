/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned opened;
static LRESULT CALLBACK owner_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_SYSCOMMAND&&(wp&0xfff0)==SC_KEYMENU){++opened;return 0;}
    return DefWindowProcA(hwnd,message,wp,lp);
}
static void drain(void)
{
    MSG msg;DWORD end=GetTickCount()+100;
    do{while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageA(&msg);}Sleep(1);}while((LONG)(GetTickCount()-end)<0);
}
START_TEST(menu_key_state)
{
    WNDCLASSA cls={0};HWND owner;POINT point={40,40};unsigned pass,button;
    const UINT messages[]={0,WM_LBUTTONDOWN,WM_RBUTTONDOWN,WM_MBUTTONDOWN,WM_MOUSEMOVE};
    const DWORD events[]={0,MOUSEEVENTF_LEFTDOWN|MOUSEEVENTF_LEFTUP,MOUSEEVENTF_RIGHTDOWN|MOUSEEVENTF_RIGHTUP,MOUSEEVENTF_MIDDLEDOWN|MOUSEEVENTF_MIDDLEUP,MOUSEEVENTF_MOVE};
    cls.lpfnWndProc=owner_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MenuKey98";
    ok(RegisterClassA(&cls)!=0,"register owner\n");
    owner=CreateWindowA(cls.lpszClassName,"menu keys",WS_OVERLAPPEDWINDOW|WS_VISIBLE,30,30,220,160,NULL,NULL,cls.hInstance,NULL);
    ok(owner!=NULL,"create owner\n");if(!owner)return;
    SetForegroundWindow(owner);SetFocus(owner);ClientToScreen(owner,&point);SetCursorPos(point.x,point.y);drain();
    for(pass=0;pass<2;++pass)for(button=0;button<5;++button){
        opened=0;
        if(!pass){
            SendMessageA(owner,WM_SYSKEYDOWN,VK_MENU,0x20380001);
            if(messages[button])SendMessageA(owner,messages[button],0,MAKELPARAM(40,40));
            SendMessageA(owner,WM_SYSKEYUP,VK_MENU,0xc0380001);
        }else{
            keybd_event(VK_MENU,0,0,0);
            if(events[button])mouse_event(events[button],0,0,0,0);
            keybd_event(VK_MENU,0,KEYEVENTF_KEYUP,0);drain();
        }
        trace("MENU_KEY pass=%u button=%u opened=%u\n",pass,button,opened);
        if(!button)ok(opened==1,"pass=%u ALT alone opens %u times\n",pass,opened);
        if(pass&&button==1)ok(!opened,"ALT with left click opens %u times\n",opened);
    }
    DestroyWindow(owner);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
