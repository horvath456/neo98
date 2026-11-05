/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND one,two;
static unsigned round;
static unsigned role(HWND h){return !h?0:h==one?1:h==two?2:h==GetDesktopWindow()?3:4;}
static void state(const char *step){trace("FOREGROUND_STATE round=%u step=%s active=%u foreground=%u focus=%u visible=%u\n",round,step,role(GetActiveWindow()),role(GetForegroundWindow()),role(GetFocus()),!!IsWindowVisible(one));fflush(stdout);}
START_TEST(foreground_state)
{
    for(round=0;round<2;++round){
        one=CreateWindowA("static","foreground",WS_OVERLAPPEDWINDOW,40,40,200,150,NULL,NULL,NULL,NULL);
        two=round?CreateWindowA("static","other",WS_OVERLAPPEDWINDOW|WS_VISIBLE,260,40,200,150,NULL,NULL,NULL,NULL):NULL;
        ok(one!=NULL,"create window\n");
        ShowWindow(one,SW_HIDE);SetFocus(NULL);SetActiveWindow(NULL);state("initial");
        ShowWindow(one,SW_SHOW);state("show");
        SetActiveWindow(NULL);state("deactivate");
        SetForegroundWindow(one);state("foreground");
        SetForegroundWindow(NULL);state("null");
        SetWindowPos(one,NULL,0,0,0,0,SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_HIDEWINDOW);state("hide-noactivate");
        SetWindowPos(one,NULL,0,0,0,0,SWP_NOZORDER|SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);state("show-noactivate");
        SetForegroundWindow(GetDesktopWindow());state("desktop");
        ShowWindow(one,SW_HIDE);state("hide");
        SetForegroundWindow(one);state("hidden-foreground");
        ShowWindow(one,SW_SHOW);state("show-again");
        if(two)DestroyWindow(two);DestroyWindow(one);one=two=NULL;
    }
}
