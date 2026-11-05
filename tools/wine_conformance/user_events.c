/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned phase, moves, commands, notifications, erases;
static LRESULT CALLBACK event_proc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_MOUSEMOVE)++moves;
    if(message==WM_ERASEBKGND){++erases;return 1;}
    if(message==WM_COMMAND){++commands;trace("EDIT_EVENT phase=%u code=%u id=%u\n",phase,HIWORD(wp),LOWORD(wp));}
    if(message==WM_PARENTNOTIFY){++notifications;trace("PARENT_EVENT phase=%u event=%u id=%u\n",phase,LOWORD(wp),HIWORD(wp));}
    if(message==WM_WINDOWPOSCHANGING){
        WINDOWPOS *p=(WINDOWPOS *)lp;
        trace("POSITION_EVENT phase=%u window=%ld flags=%04x after=%s\n",phase,GetWindowLongA(window,GWL_ID),p->flags,
              p->hwndInsertAfter==HWND_TOPMOST?"TOPMOST":p->hwndInsertAfter==HWND_TOP?"TOP":"other");
    }
    return DefWindowProcA(window,message,wp,lp);
}
static void drain(void)
{
    DWORD end=GetTickCount()+100;MSG msg;
    do{
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE))DispatchMessageA(&msg);
        Sleep(1);
    }while((LONG)(end-GetTickCount())>0);
}
START_TEST(user_events)
{
    WNDCLASSA cls;HWND parent,child,popup,edit;unsigned i,j;
    const char *titles[]={NULL,"","hello"};POINT old;
    memset(&cls,0,sizeof cls);cls.lpfnWndProc=event_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="UserEvents";
    RegisterClassA(&cls);GetCursorPos(&old);
    parent=CreateWindowA(cls.lpszClassName,"events",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,300,220,NULL,NULL,cls.hInstance,NULL);
    ok(parent!=NULL,"parent creation\n");
    SetWindowLongA(parent,GWL_ID,10);
    child=CreateWindowA(cls.lpszClassName,"child",WS_CHILD,0,0,40,40,parent,(HMENU)20,cls.hInstance,NULL);
    phase=1;popup=CreateWindowExA(WS_EX_TOPMOST,cls.lpszClassName,"popup",WS_POPUP,0,0,40,40,child,NULL,cls.hInstance,NULL);
    ok(popup!=NULL,"popup creation\n");SetWindowLongA(popup,GWL_ID,30);
    phase=2;SetWindowPos(popup,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    phase=3;SetWindowPos(popup,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    DestroyWindow(popup);DestroyWindow(child);
    for(i=0;i<2;++i)for(j=0;j<3;++j){
        phase=10+i*3+j;commands=notifications=0;
        edit=CreateWindowA("EDIT",titles[j],WS_CHILD|(i?ES_MULTILINE:0),0,0,100,50,parent,(HMENU)42,cls.hInstance,NULL);
        ok(edit!=NULL,"edit creation phase=%u\n",phase);
        trace("EDIT_CREATED phase=%u commands=%u parent_notifications=%u\n",phase,commands,notifications);
        ok(commands==(!i&&j==2?2:0)&&notifications==1,"creation events phase=%u: commands=%u parent=%u\n",phase,commands,notifications);
        DestroyWindow(edit);
    }
    phase=20;SetCursorPos(100,100);drain();
    for(i=0;i<3;++i){moves=0;SetCursorPos(100,100);drain();ok(moves!=0,"same-position cursor iteration=%u moves=%u\n",i,moves);}
    for(i=0;i<3;++i){
        unsigned flags=SWP_SHOWWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE;
        phase=30+i;
        child=CreateWindowA("STATIC","",WS_CHILD,10,10,40,40,parent,(HMENU)20,cls.hInstance,NULL);
        RedrawWindow(parent,NULL,NULL,RDW_VALIDATE|RDW_ALLCHILDREN);
        InvalidateRect(parent,NULL,TRUE);erases=0;
        if(i==1)flags|=SWP_DEFERERASE;
        if(i==2)flags|=SWP_NOREDRAW;
        SetWindowPos(child,NULL,0,0,0,0,flags);
        ok(erases==(i==0?1:0),"parent erase during child show phase=%u erases=%u\n",phase,erases);
        DestroyWindow(child);
    }
    for(i=0;i<4;++i){
        phase=40+i;
        child=CreateWindowA(i&1?"EDIT":"STATIC","hello",WS_CHILD|WS_BORDER|((i&2)?WS_CLIPSIBLINGS:0),
            0,0,16384,20,parent,(HMENU)20,cls.hInstance,NULL);
        UpdateWindow(parent);RedrawWindow(parent,NULL,NULL,RDW_VALIDATE|RDW_ALLCHILDREN);
        SetWindowPos(child,NULL,30,15,60,20,SWP_NOZORDER|SWP_NOACTIVATE);
        SetFocus(child);UpdateWindow(parent);erases=0;
        ShowWindow(child,SW_SHOW);
        trace("CLEAN_PARENT_SHOW phase=%u class=%s clipsiblings=%u erases=%u update=%u\n",phase,
            i&1?"EDIT":"STATIC",!!(i&2),erases,GetUpdateRect(parent,NULL,FALSE));
        ok(erases==1 && GetUpdateRect(parent,NULL,FALSE),"new child parent erase phase=%u erases=%u\n",phase,erases);
        DestroyWindow(child);
    }
    SetCursorPos(old.x,old.y);DestroyWindow(parent);
}
