/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static void flush(void)
{
    MSG msg;DWORD start=GetTickCount();
    do{while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);Sleep(10);}
    while(GetTickCount()-start<80);
}
START_TEST(quit_metadata)
{
    WNDCLASSA cls={0};HWND window;POINT saved;unsigned kind,remove;
    cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="QuitMetadata";
    ok(RegisterClassA(&cls)!=0,"register class\n");
    window=CreateWindowA(cls.lpszClassName,"Quit metadata",WS_POPUP|WS_VISIBLE,50,50,400,300,NULL,NULL,cls.hInstance,NULL);
    ok(window!=NULL,"create window\n");if(!window)return;
    GetCursorPos(&saved);SetForegroundWindow(window);SetCapture(window);SetCursorPos(123,145);flush();
    for(kind=0;kind<3;++kind){
        DWORD post_before,post_after,oldtime,oldpos;LPARAM oldextra;
        flush();oldtime=GetMessageTime();oldpos=GetMessagePos();SetMessageExtraInfo(0x1234abcd);oldextra=GetMessageExtraInfo();
        post_before=GetTickCount();
        if(!kind)PostQuitMessage(7);
        else ok(PostThreadMessageA(GetCurrentThreadId(),kind==1?WM_QUIT:WM_APP,7,8),"post kind %u\n",kind);
        post_after=GetTickCount();Sleep(40);
        for(remove=0;remove<2;++remove){
            MSG msg={0};DWORD before=GetTickCount(),after;BOOL found;
            found=PeekMessageA(&msg,NULL,0,0,remove?PM_REMOVE:PM_NOREMOVE);after=GetTickCount();
            trace("QUIT_META kind=%u remove=%u found=%u message=%04x time_current=%u time_posted=%u point_same=%u getter_old=%u getter_message=%u extra_old=%u extra_zero=%u\n",
                  kind,remove,!!found,msg.message,(DWORD)(msg.time-before)<=(DWORD)(after-before),
                  (DWORD)(msg.time-post_before)<=(DWORD)(post_after-post_before),
                  MAKELONG((WORD)msg.pt.x,(WORD)msg.pt.y)==oldpos,
                  (DWORD)GetMessageTime()==oldtime,(DWORD)GetMessageTime()==msg.time,
                  GetMessageExtraInfo()==oldextra,GetMessageExtraInfo()==0);
        }
    }
    ReleaseCapture();DestroyWindow(window);SetCursorPos(saved.x,saved.y);flush();
}
