/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND list;
static WNDPROC original;
static unsigned recording,used;
static char events[2048];
static void event(unsigned message,unsigned value)
{
    if(recording && used+24<sizeof events)used+=sprintf(events+used,"%x:%x,",message,value);
}
static LRESULT CALLBACK hook(int code,WPARAM w,LPARAM l)
{
    if(code==HCBT_SETFOCUS)event(0x10009,w==(WPARAM)list);
    return CallNextHookEx(NULL,code,w,l);
}
static LRESULT CALLBACK parent_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m>=WM_CTLCOLORMSGBOX && m<=WM_CTLCOLORSTATIC)event(m,1);
    if(m==WM_COMMAND)event(m,HIWORD(w));
    if(m==WM_MEASUREITEM){((MEASUREITEMSTRUCT*)l)->itemHeight=12;return TRUE;}
    if(m==WM_DRAWITEM){DRAWITEMSTRUCT*d=(DRAWITEMSTRUCT*)l;event(m,(d->itemID<<16)|(d->itemAction<<8)|d->itemState);return TRUE;}
    return DefWindowProcA(h,m,w,l);
}
static LRESULT CALLBACK list_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_SETFOCUS || m==WM_KILLFOCUS || m==WM_CAPTURECHANGED)event(m,0);
    return CallWindowProcA(original,h,m,w,l);
}
START_TEST(listbox_messages)
{
    WNDCLASSA cls={0};HWND parent;HHOOK hhook;unsigned visible,kind,step;
    cls.lpfnWndProc=parent_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="ListMessagesParent";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        20,20,320,240,NULL,NULL,cls.hInstance,NULL);ok(parent!=NULL,"parent\n");
    hhook=SetWindowsHookExA(WH_CBT,hook,NULL,GetCurrentThreadId());ok(hhook!=NULL,"hook\n");
    for(visible=0;visible<2;++visible)for(kind=0;kind<3;++kind) {
        DWORD style=WS_CHILD|LBS_NOTIFY|LBS_HASSTRINGS|(visible?WS_VISIBLE:0)|
            (kind==1?LBS_OWNERDRAWFIXED:kind==2?LBS_OWNERDRAWVARIABLE:0);
        list=CreateWindowA("LISTBOX","",style,10,10,160,80,parent,(HMENU)10,cls.hInstance,NULL);
        ok(list!=NULL,"list %u/%u\n",visible,kind);
        original=(WNDPROC)SetWindowLongA(list,GWL_WNDPROC,(LONG)list_proc);
        SendMessageA(list,LB_ADDSTRING,0,(LPARAM)"alpha");
        SendMessageA(list,LB_ADDSTRING,0,(LPARAM)"bravo");
        SendMessageA(list,LB_ADDSTRING,0,(LPARAM)"charlie");
        UpdateWindow(list);SetFocus(NULL);
        for(step=0;step<10;++step) {
            LRESULT result=0;used=0;events[0]=0;recording=1;
            if(step<3)result=SendMessageA(list,LB_SETCURSEL,step,0);
            if(step==3)SetFocus(list);
            if(step==4)result=SendMessageA(list,WM_LBUTTONDOWN,0,MAKELPARAM(1,1));
            if(step==5)result=SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(1,1));
            if(step==6)result=SendMessageA(list,LB_SETCURSEL,2,0);
            if(step==7)result=SendMessageA(list,LB_SETCURSEL,1,0);
            if(step==8)result=SendMessageA(list,WM_LBUTTONDOWN,0,MAKELPARAM(1,SendMessageA(list,LB_GETITEMHEIGHT,0,0)+1));
            if(step==9)result=SendMessageA(list,WM_LBUTTONUP,0,MAKELPARAM(1,1));
            recording=0;
            trace("LIST_MESSAGES visible=%u kind=%u step=%u result=%ld selected=%ld focus=%u capture=%u events=%s\n",
                visible,kind,step,result,SendMessageA(list,LB_GETCURSEL,0,0),GetFocus()==list,GetCapture()==list,events);
            fflush(stdout);
        }
        DestroyWindow(list);list=NULL;
    }
    UnhookWindowsHookEx(hhook);DestroyWindow(parent);
}
