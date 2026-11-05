/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <commctrl.h>
#include "wine/test.h"
static unsigned erases,position_changing,position_changed;
static LRESULT CALLBACK erase_proc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_ERASEBKGND){++erases;return 1;}
    return DefWindowProcA(window,message,wp,lp);
}
static void ancestor_erase(void)
{
    WNDCLASSA cls;unsigned clipped;
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=erase_proc;cls.hInstance=GetModuleHandleA(NULL);
    cls.lpszClassName="EraseContract";RegisterClassA(&cls);
    for(clipped=0;clipped<2;++clipped){
        HWND parent=CreateWindowA(cls.lpszClassName,"",WS_VISIBLE|WS_POPUP|(clipped?WS_CLIPCHILDREN:0),
            200,100,120,120,NULL,NULL,cls.hInstance,NULL);
        HWND child=CreateWindowA(cls.lpszClassName,"",WS_VISIBLE|WS_CHILD,0,0,60,60,parent,NULL,cls.hInstance,NULL);
        ok(parent&&child,"erase windows unavailable\n");
        if(!parent||!child)return;
        UpdateWindow(parent);UpdateWindow(child);
        RedrawWindow(parent,NULL,NULL,RDW_INVALIDATE|RDW_NOCHILDREN);
        erases=0;RedrawWindow(child,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ERASENOW);
        ok(erases==clipped,"ancestor pending, clipchildren=%u: %u erases\n",clipped,erases);
        RedrawWindow(parent,NULL,NULL,RDW_VALIDATE|RDW_NOCHILDREN);
        RedrawWindow(child,NULL,NULL,RDW_ERASENOW);
        ok(erases==1,"after ancestor validation, clipchildren=%u: %u erases\n",clipped,erases);
        DestroyWindow(child);DestroyWindow(parent);
    }
}
static LRESULT CALLBACK parent_proc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_WINDOWPOSCHANGING)++position_changing;
    if(message==WM_WINDOWPOSCHANGED)++position_changed;
    if(message==WM_WINDOWPOSCHANGING||message==WM_WINDOWPOSCHANGED||message==WM_ACTIVATEAPP||
       message==WM_ACTIVATE||message==WM_NCACTIVATE||message==WM_SETFOCUS||message==WM_KILLFOCUS)
        trace("LV_PARENT message=%04x wp=%lu\n",message,(unsigned long)wp);
    return DefWindowProcA(window,message,wp,lp);
}
static void topmost_order(void)
{
    HWND anchor=CreateWindowExA(WS_EX_TOPMOST,"STATIC","",WS_POPUP,-20,-20,1,1,NULL,NULL,NULL,NULL);
    HWND window;
    ShowWindow(anchor,SW_SHOWNOACTIVATE);
    position_changing=position_changed=0;
    window=CreateWindowA("ListContract","",WS_OVERLAPPEDWINDOW|WS_VISIBLE,200,100,120,120,NULL,NULL,GetModuleHandleA(NULL),NULL);
    ok(anchor&&window,"topmost order windows unavailable\n");
    ok(position_changing==2 && position_changed==1,"creation below topmost: changing=%u changed=%u\n",position_changing,position_changed);
    position_changing=position_changed=0;
    SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    ok(position_changing==1 && position_changed==0,"unchanged order below topmost: changing=%u changed=%u\n",position_changing,position_changed);
    DestroyWindow(window);DestroyWindow(anchor);
}
static void geometry(HWND list,unsigned phase)
{
    unsigned i;RECT r,view;POINT origin;LOGFONTA lf;TEXTMETRICA tm;SIZE extent;
    HFONT font=(HFONT)SendMessageA(list,WM_GETFONT,0,0);HDC dc=GetDC(list);HGDIOBJ old=NULL;
    if(font)old=SelectObject(dc,font);
    memset(&lf,0,sizeof lf);memset(&tm,0,sizeof tm);
    GetObjectA(font,sizeof lf,&lf);GetTextMetricsA(dc,&tm);GetTextExtentPoint32A(dc,"item1",5,&extent);
    GetClientRect(list,&r);SendMessageA(list,LVM_GETVIEWRECT,0,(LPARAM)&view);SendMessageA(list,LVM_GETORIGIN,0,(LPARAM)&origin);
    trace("LV_GEOMETRY phase=%u client=%ld,%ld view=%ld,%ld,%ld,%ld origin=%ld,%ld font=%s/%ld/%ld metrics=%ld/%ld text=%ld,%ld\n",
        phase,r.right,r.bottom,view.left,view.top,view.right,view.bottom,origin.x,origin.y,
        lf.lfFaceName,lf.lfHeight,lf.lfWidth,tm.tmHeight,tm.tmAveCharWidth,extent.cx,extent.cy);
    for(i=0;i<3;++i){memset(&r,0,sizeof r);r.left=LVIR_BOUNDS;SendMessageA(list,LVM_GETITEMRECT,i,(LPARAM)&r);
        trace("LV_ITEM phase=%u item=%u rect=%ld,%ld,%ld,%ld\n",phase,i,r.left,r.top,r.right,r.bottom);}
    if(old)SelectObject(dc,old);ReleaseDC(list,dc);
}
START_TEST(listview_contract)
{
    WNDCLASSA cls;HWND parent,list;RECT r;LVITEMA item;unsigned i;
    HMODULE module=LoadLibraryA("COMCTL32.DLL");void (WINAPI *initialize)(void)=(void*)GetProcAddress(module,"InitCommonControls");
    ok(initialize!=NULL,"common controls unavailable\n");if(!initialize)return;initialize();
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=parent_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ListContract";
    cls.hbrBackground=GetStockObject(WHITE_BRUSH);RegisterClassA(&cls);
    parent=CreateWindowExA(0,cls.lpszClassName,"List",WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_VISIBLE,
        0,0,100,100,GetDesktopWindow(),NULL,cls.hInstance,NULL);
    GetClientRect(parent,&r);list=CreateWindowExA(0,WC_LISTVIEWA,"foo",WS_CHILD|WS_BORDER|WS_VISIBLE|LVS_ICON,
        0,0,r.right,r.bottom,parent,NULL,cls.hInstance,NULL);
    ok(parent&&list,"window setup failed\n");if(!parent||!list)return;
    memset(&item,0,sizeof item);item.mask=LVIF_TEXT;item.pszText="item0";
    for(i=0;i<3;++i){item.iItem=i;SendMessageA(list,LVM_INSERTITEMA,0,(LPARAM)&item);}
    geometry(list,0);SendMessageA(list,LVM_SETITEMPOSITION,1,MAKELPARAM(10,5));geometry(list,1);
    SendMessageA(list,LVM_SETITEMPOSITION,2,0);geometry(list,2);
    SendMessageA(list,LVM_SETITEMPOSITION,0,MAKELPARAM(20,20));geometry(list,3);
    DestroyWindow(list);DestroyWindow(parent);ancestor_erase();topmost_order();fflush(stdout);
}
