/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static void peek(const char *name,unsigned first,unsigned last,unsigned remove)
{
    MSG msg={0};BOOL ret=PeekMessageA(&msg,NULL,first,last,remove);
    trace("QUIT_PEEK name=%s found=%u message=%04x wp=%08lx lp=%08lx\n",name,!!ret,msg.message,msg.wParam,msg.lParam);
}
static void status(const char *name)
{ trace("QUIT_STATUS name=%s bits=%08lx\n",name,GetQueueStatus(QS_POSTMESSAGE)); }
START_TEST(quit_contract)
{
    MSG msg;DWORD thread=GetCurrentThreadId();
    while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){}
    status("empty");PostQuitMessage(0x1234abcd);status("posted-quit");
    peek("quit-peek",0,0,PM_NOREMOVE);status("after-peek");
    ok(PostThreadMessageA(thread,WM_USER,1,2),"post ordinary thread message\n");
    status("ordinary-after-quit");peek("ordinary-first",0,0,PM_REMOVE);status("quit-remains");
    peek("quit-last",0,0,PM_REMOVE);status("quit-removed");peek("empty-after-quit",0,0,PM_REMOVE);status("empty-after-read");
    PostQuitMessage(11);PostQuitMessage(22);peek("latest-code",0,0,PM_REMOVE);peek("no-second-quit",0,0,PM_REMOVE);
    ok(PostThreadMessageA(thread,WM_QUIT,0x87654321,0x76543210),"post explicit quit node\n");
    ok(PostThreadMessageA(thread,WM_USER,3,4),"post behind explicit quit\n");
    peek("explicit-quit-first",0,0,PM_REMOVE);peek("ordinary-second",0,0,PM_REMOVE);
    PostQuitMessage(33);ok(PostThreadMessageA(thread,WM_USER,5,6),"post filtered ordinary message\n");
    peek("filtered-with-posted",WM_APP,WM_APP,PM_REMOVE);
    peek("remove-posted",WM_USER,WM_USER,PM_REMOVE);
    peek("filtered-quit-only",WM_APP,WM_APP,PM_REMOVE);peek("final-empty",0,0,PM_REMOVE);
}
