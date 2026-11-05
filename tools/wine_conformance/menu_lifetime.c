/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned entered;
static LRESULT CALLBACK owner_proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_ENTERMENULOOP){++entered;PostMessageA(hwnd,WM_CANCELMODE,0,0);}
    return DefWindowProcA(hwnd,message,wp,lp);
}
static void inspect(HMENU menu,const char *phase)
{
    MENUITEMINFOA info={0};BOOL valid,result;int count;UINT state;
    valid=IsMenu(menu);count=GetMenuItemCount(menu);state=GetMenuState(menu,0,MF_BYPOSITION);
    info.cbSize=sizeof info;info.fMask=MIIM_ID|MIIM_STATE|MIIM_TYPE;
    SetLastError(0x12345678);result=GetMenuItemInfoA(menu,0,TRUE,&info);
    trace("MENU_LIFETIME %s valid=%u count=%d state=%u info=%u error=%lu id=%u\n",
          phase,valid,count,state,result,GetLastError(),info.wID);
}
START_TEST(menu_lifetime)
{
    WNDCLASSA cls={0};HWND owner;HMENU menu;BOOL result;
    cls.lpfnWndProc=owner_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MenuLifetime98";
    ok(RegisterClassA(&cls)!=0,"register owner\n");
    owner=CreateWindowA(cls.lpszClassName,"menu lifetime",WS_VISIBLE,30,30,220,160,NULL,NULL,cls.hInstance,NULL);
    ok(owner!=NULL,"create owner\n");if(!owner)return;
    menu=CreateMenu();AppendMenuA(menu,MF_STRING,10,"&Test");SetMenu(owner,menu);
    inspect(menu,"attached");
    result=DestroyMenu(menu);ok(result,"destroy attached menu\n");inspect(menu,"destroyed");
    DrawMenuBar(owner);SendMessageA(owner,WM_SYSCOMMAND,SC_KEYMENU,0);
    ok(entered==1,"destroyed attached menu still enters, entered=%u\n",entered);
    result=AppendMenuA(menu,MF_STRING,11,"Other");trace("MENU_LIFETIME append=%u\n",result);
    inspect(menu,"after-append");result=DestroyMenu(menu);trace("MENU_LIFETIME second-destroy=%u\n",result);
    SetMenu(owner,NULL);inspect(menu,"detached");
    DestroyWindow(owner);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
