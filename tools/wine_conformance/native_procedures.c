/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static WNDPROC original;
static unsigned created,destroyed;
static LRESULT CALLBACK mdi_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    if(msg==WM_CREATE)++created;
    if(msg==WM_DESTROY)++destroyed;
    return CallWindowProcA(original,hwnd,msg,wp,lp);
}
START_TEST(native_procedures)
{
    WNDCLASSA cls={0};CLIENTCREATESTRUCT client={0};HWND frame,mdi;BOOL result;
    result=GetClassInfoA(NULL,"MDICLIENT",&cls);
    ok(result,"get native MDI class\n");if(!result)return;
    original=cls.lpfnWndProc;cls.lpfnWndProc=mdi_proc;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="MdiSuper98";
    ok(RegisterClassA(&cls)!=0,"register MDI superclass\n");
    frame=CreateWindowA("STATIC","frame",WS_OVERLAPPEDWINDOW,20,20,200,160,NULL,NULL,cls.hInstance,NULL);
    ok(frame!=NULL,"create frame\n");if(!frame)return;
    client.idFirstChild=100;
    mdi=CreateWindowA(cls.lpszClassName,"",WS_CHILD|WS_VISIBLE,0,0,160,120,frame,NULL,cls.hInstance,&client);
    ok(mdi!=NULL,"create MDI superclass\n");
    ok(created==1,"WM_CREATE calls=%u\n",created);
    if(mdi){
        ok(SendMessageA(mdi,WM_MDIGETACTIVE,0,0)==0,"new client has no active child\n");
        DestroyWindow(mdi);ok(destroyed==1,"WM_DESTROY calls=%u\n",destroyed);
    }
    DestroyWindow(frame);UnregisterClassA(cls.lpszClassName,cls.hInstance);
}
