/* SPDX-License-Identifier: GPL-2.0-or-later
 * Geometry observations use the same executable on Win98 and NEO98. */
#include <windows.h>
#include "wine/test.h"

static void snapshot(HWND hwnd, unsigned kind, unsigned step)
{
    RECT r, c;
    WINDOWPLACEMENT p;
    memset(&p, 0, sizeof p); p.length = sizeof p;
    GetWindowRect(hwnd, &r); GetClientRect(hwnd, &c);
    GetWindowPlacement(hwnd, &p);
    trace("WINDOW_GEOMETRY %u %u style=%08lx rect=%ld,%ld,%ld,%ld client=%ld,%ld,%ld,%ld show=%u min=%ld,%ld max=%ld,%ld normal=%ld,%ld,%ld,%ld\n",
          kind, step, GetWindowLongA(hwnd,GWL_STYLE), r.left,r.top,r.right,r.bottom,
          c.left,c.top,c.right,c.bottom,p.showCmd,p.ptMinPosition.x,p.ptMinPosition.y,
          p.ptMaxPosition.x,p.ptMaxPosition.y,p.rcNormalPosition.left,p.rcNormalPosition.top,
          p.rcNormalPosition.right,p.rcNormalPosition.bottom);
    fflush(stdout);
}

START_TEST(windows)
{
    HWND parent, hwnd;
    unsigned kind, i;
    const unsigned commands[]={SW_SHOW,SW_MINIMIZE,SW_RESTORE,SW_MAXIMIZE,SW_RESTORE};
    const DWORD styles[]={WS_OVERLAPPEDWINDOW,WS_POPUP|WS_CAPTION,WS_CHILD|WS_CAPTION,
                          WS_CHILD|WS_MAXIMIZE,WS_CHILD|WS_MINIMIZE};
    parent=CreateWindowA("STATIC","reference parent",WS_OVERLAPPEDWINDOW,
                         40,50,400,300,NULL,NULL,NULL,NULL);
    ok(parent != NULL,"parent creation failed\n");
    for(kind=0;kind<sizeof(styles)/sizeof(styles[0]);++kind) {
        hwnd=CreateWindowA("STATIC","geometry",styles[kind],10,20,180,100,
                           styles[kind]&WS_CHILD?parent:NULL,NULL,NULL,NULL);
        ok(hwnd != NULL,"window creation failed, kind %u\n",kind);
        if(!hwnd)continue;
        snapshot(hwnd,kind,0);
        for(i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
            ShowWindow(hwnd,commands[i]); snapshot(hwnd,kind,i+1);
        }
        DestroyWindow(hwnd);
    }
    DestroyWindow(parent);
    {
        const DWORD extra_styles[]={0x04000000,0x84000000,0,WS_POPUP};
        for(kind=0;kind<4;++kind) {
            hwnd=CreateWindowA("STATIC","frame and negative sizes",extra_styles[kind],
                              40,50,kind<2?150:-2,kind<2?90:-2,NULL,NULL,NULL,NULL);
            ok(hwnd != NULL,"extra window creation failed, kind %u\n",kind);
            if(!hwnd)continue;
            snapshot(hwnd,kind+5,0);
            if(kind<2) {
                SetWindowLongA(hwnd,GWL_STYLE,extra_styles[kind]|WS_BORDER);
                SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
                snapshot(hwnd,kind+5,1);
            }
            DestroyWindow(hwnd);
        }
    }
}
