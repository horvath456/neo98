/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned entered,idle,timedout,reason,valid_popup;
static LRESULT CALLBACK owner_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_ENTERMENULOOP)++entered;
    if(msg==WM_ENTERIDLE){++idle;reason=wp;valid_popup=IsWindow((HWND)lp);PostMessageA(hwnd,WM_CANCELMODE,0,0);}
    if(msg==WM_TIMER){++timedout;EndMenu();}
    return DefWindowProcA(hwnd,msg,wp,lp);
}
START_TEST(menu_idle)
{
    WNDCLASSA cls={0};HWND owner;HMENU menu;unsigned pass;BOOL result;
    cls.lpfnWndProc=owner_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MenuIdle98";
    ok(RegisterClassA(&cls)!=0,"register menu owner\n");
    owner=CreateWindowA(cls.lpszClassName,"menu idle",WS_OVERLAPPEDWINDOW|WS_VISIBLE,30,30,220,160,NULL,NULL,cls.hInstance,NULL);
    ok(owner!=NULL,"create menu owner\n");if(!owner)return;
    menu=CreatePopupMenu();AppendMenuA(menu,MF_STRING,1,"One");
    for(pass=0;pass<2;++pass){
        entered=idle=timedout=reason=valid_popup=0;
        SetTimer(owner,1,1000,NULL);
        result=TrackPopupMenu(menu,pass?TPM_NONOTIFY:0,60,80,0,owner,NULL);
        KillTimer(owner,1);
        trace("MENU_IDLE pass=%u result=%u entered=%u idle=%u timedout=%u reason=%u popup=%u\n",pass,result,entered,idle,timedout,reason,valid_popup);
        ok(entered==!pass,"pass=%u enter notifications=%u\n",pass,entered);
        ok(idle==1&&!timedout,"pass=%u idle=%u timeout=%u\n",pass,idle,timedout);
        ok(reason==MSGF_MENU&&valid_popup,"pass=%u idle reason=%u valid popup=%u\n",pass,reason,valid_popup);
    }
    DestroyMenu(menu);DestroyWindow(owner);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
