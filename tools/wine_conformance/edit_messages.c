/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND edit;
static WNDPROC original;
static unsigned recording,used;
static char events[2048];
static void event(unsigned message,unsigned value)
{
    if(recording && used+24<sizeof events)used+=sprintf(events+used,"%x:%x,",message,value);
}
static LRESULT CALLBACK hook(int code,WPARAM w,LPARAM l)
{
    if(code==HCBT_SETFOCUS)event(0x10009,w==(WPARAM)edit);
    return CallNextHookEx(NULL,code,w,l);
}
static LRESULT CALLBACK parent_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m>=WM_CTLCOLORMSGBOX && m<=WM_CTLCOLORSTATIC)event(m,1);
    if(m==WM_COMMAND)event(m,HIWORD(w));
    return DefWindowProcA(h,m,w,l);
}
static LRESULT CALLBACK edit_proc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_SETFOCUS || m==WM_KILLFOCUS || m==WM_CAPTURECHANGED || m==WM_PAINT || m==WM_ERASEBKGND)
        event(m,0);
    return CallWindowProcA(original,h,m,w,l);
}
START_TEST(edit_messages)
{
    WNDCLASSA cls;HWND parent;HHOOK hhook;unsigned visible,kind,step;
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=parent_proc;
    cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="EditMessagesParent";
    ok(RegisterClassA(&cls)!=0,"class\n");
    parent=CreateWindowA(cls.lpszClassName,"parent",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        20,20,320,240,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"parent\n");
    hhook=SetWindowsHookExA(WH_CBT,hook,NULL,GetCurrentThreadId());ok(hhook!=NULL,"hook\n");
    for(visible=0;visible<2;++visible)for(kind=0;kind<8;++kind) {
        DWORD style=WS_CHILD|(visible?WS_VISIBLE:0)|((kind&1)?ES_MULTILINE:0)|
            ((kind&2)?ES_READONLY:0)|((kind&4)?WS_DISABLED:0);
        edit=CreateWindowA("EDIT","alpha beta",style,10,10,160,40,parent,(HMENU)10,cls.hInstance,NULL);
        ok(edit!=NULL,"edit %u/%u\n",visible,kind);
        original=(WNDPROC)SetWindowLongA(edit,GWL_WNDPROC,(LONG)edit_proc);
        SendMessageA(edit,WM_SETFONT,(WPARAM)GetStockObject(SYSTEM_FIXED_FONT),TRUE);
        UpdateWindow(edit);SetFocus(NULL);SendMessageA(edit,EM_SETSEL,0,0);
        for(step=0;step<8;++step) {
            unsigned result=0;DWORD first=0,last=0;
            if(step==6){SetFocus(NULL);ReleaseCapture();SendMessageA(edit,EM_SETSEL,0,0);}
            used=0;events[0]=0;recording=1;
            switch(step) {
            case 0:result=SendMessageA(edit,WM_GETDLGCODE,0,0);break;
            case 1:SetFocus(edit);break;
            case 2:SetFocus(NULL);break;
            case 3:SendMessageA(edit,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(12,4));break;
            case 4:SendMessageA(edit,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(35,4));break;
            case 5:SendMessageA(edit,WM_LBUTTONUP,0,MAKELPARAM(35,4));break;
            case 6:SendMessageA(edit,WM_LBUTTONDBLCLK,0,MAKELPARAM(12,4));break;
            case 7:SendMessageA(edit,WM_LBUTTONUP,0,MAKELPARAM(12,4));break;
            }
            recording=0;SendMessageA(edit,EM_GETSEL,(WPARAM)&first,(LPARAM)&last);
            trace("EDIT_MESSAGES visible=%u kind=%u step=%u result=%x focus=%u capture=%u sel=%lu,%lu events=%s\n",
                visible,kind,step,result,GetFocus()==edit,GetCapture()==edit,first,last,events);
            fflush(stdout);
        }
        DestroyWindow(edit);edit=NULL;
    }
    UnhookWindowsHookEx(hhook);DestroyWindow(parent);
}
