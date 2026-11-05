/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HCURSOR wait_cursor,arrow;
static unsigned kind(HCURSOR cursor){return !cursor?0:cursor==wait_cursor?1:cursor==arrow?2:3;}
static DWORD WINAPI worker(void *unused)
{
    HCURSOR old;trace("CURSOR_STATE worker initial=%u\n",kind(GetCursor()));
    old=SetCursor(wait_cursor);trace("CURSOR_STATE worker setwait old=%u current=%u\n",kind(old),kind(GetCursor()));
    old=SetCursor(NULL);trace("CURSOR_STATE worker setnull old=%u current=%u\n",kind(old),kind(GetCursor()));
    return 0;
}
START_TEST(cursor_state)
{
    HCURSOR initial=GetCursor(),old;HANDLE thread;DWORD id;
    wait_cursor=LoadCursorA(NULL,IDC_WAIT);arrow=LoadCursorA(NULL,IDC_ARROW);
    ok(wait_cursor&&arrow,"system cursors\n");
    trace("CURSOR_STATE main initial=%u\n",kind(initial));
    old=SetCursor(arrow);trace("CURSOR_STATE main setarrow old=%u current=%u\n",kind(old),kind(GetCursor()));
    thread=CreateThread(NULL,0,worker,NULL,0,&id);ok(thread!=NULL,"worker\n");
    if(thread){ok(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"worker completion\n");CloseHandle(thread);}
    trace("CURSOR_STATE main afterworker=%u\n",kind(GetCursor()));
    old=SetCursor(NULL);trace("CURSOR_STATE main setnull old=%u current=%u\n",kind(old),kind(GetCursor()));
}
