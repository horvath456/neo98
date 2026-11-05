/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND frame, client, child;
static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{ return DefFrameProcA(h, client, m, w, l); }
static LRESULT CALLBACK child_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{ return DefMDIChildProcA(h, m, w, l); }
static HMENU menu(void)
{
    HMENU bar=CreateMenu(), popup=CreatePopupMenu();
    ok(bar && popup, "menus\n");
    ok(AppendMenuA(popup,MF_STRING,1234,"Item"),"item\n");
    ok(AppendMenuA(bar,MF_POPUP,(UINT_PTR)popup,"File"),"popup\n");
    return bar;
}
static void state(const char *step, HMENU bar)
{
    char items[2048]={0}; unsigned used=0; int i,count=GetMenuItemCount(bar);
    HMENU system=IsWindow(child)?GetSystemMenu(child,FALSE):NULL;
    for(i=0;i<count && i<16;++i){
        MENUITEMINFOA info;unsigned bitmap;
        ZeroMemory(&info,sizeof info);info.cbSize=sizeof info;
        info.fMask=MIIM_ID|MIIM_FTYPE|MIIM_STATE|MIIM_SUBMENU|MIIM_BITMAP;
        ok(GetMenuItemInfoA(bar,i,TRUE,&info),"item %s %d\n",step,i);
        bitmap=(unsigned)(UINT_PTR)info.hbmpItem;
        if(bitmap>11)bitmap=99;
        used+=sprintf(items+used,"%u/%x/%x/%u/%u/%u,",info.hSubMenu?0:info.wID,
                      info.fType,info.fState,!!info.hSubMenu,!!system&&info.hSubMenu==system,bitmap);
    }
    trace("MDI_MENU step=%s alive=%u zoomed=%u attached=%u count=%d items=%s\n",step,
          !!IsWindow(child),!!IsZoomed(child),GetMenu(frame)==bar,count,items);
    fflush(stdout);
}
START_TEST(mdi_menu)
{
    WNDCLASSA cls={0};CLIENTCREATESTRUCT create={NULL,100};HMENU first=menu(),second;
    cls.hInstance=GetModuleHandleA(NULL);cls.lpfnWndProc=frame_proc;cls.lpszClassName="MdiMenuFrame98";
    ok(RegisterClassA(&cls)!=0,"frame class\n");
    cls.lpfnWndProc=child_proc;cls.lpszClassName="MdiMenuChild98";
    ok(RegisterClassA(&cls)!=0,"child class\n");
    frame=CreateWindowA("MdiMenuFrame98","frame",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,350,260,NULL,first,cls.hInstance,NULL);
    client=CreateWindowA("MDICLIENT",NULL,WS_CHILD|WS_VISIBLE|MDIS_ALLCHILDSTYLES,0,0,300,200,frame,NULL,cls.hInstance,&create);
    ok(frame&&client,"frame and client\n");state("initial",first);
    child=CreateWindowExA(WS_EX_MDICHILD,"MdiMenuChild98","child",WS_CHILD|WS_VISIBLE|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,
                         10,10,180,120,client,NULL,cls.hInstance,NULL);
    ok(child!=NULL,"child\n");state("normal",first);
    ShowWindow(child,SW_MAXIMIZE);state("max",first);
    SendMessageA(frame,WM_COMMAND,SC_RESTORE,0);state("restore-command",first);
    ShowWindow(child,SW_MAXIMIZE);state("max-again",first);
    second=menu();SendMessageA(client,WM_MDISETMENU,(WPARAM)second,0);
    state("replace",second);state("old-menu",first);
    SendMessageA(frame,WM_COMMAND,SC_CLOSE,0);state("close-command",second);
    DestroyWindow(frame);
    if(IsMenu(first))DestroyMenu(first);
    if(IsMenu(second))DestroyMenu(second);
}
