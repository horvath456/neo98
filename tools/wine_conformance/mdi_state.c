/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static HWND frame,client,one,two;
static unsigned tag(HWND h){return !h?0:h==frame?1:h==client?2:h==one?3:h==two?4:5;}
static LRESULT CALLBACK frame_proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefFrameProcA(h,client,m,w,l);}
static LRESULT CALLBACK child_proc(HWND h,UINT m,WPARAM w,LPARAM l){return DefMDIChildProcA(h,m,w,l);}
static HWND child(DWORD style)
{
    HWND h=CreateWindowExA(WS_EX_MDICHILD,"MdiStateChild","child",WS_CHILD|style,0,0,CW_USEDEFAULT,CW_USEDEFAULT,client,NULL,GetModuleHandleA(NULL),NULL);
    ok(h!=NULL,"child creation\n");return h;
}
static void state(const char *step)
{
    BOOL zoomed=99;HWND active=(HWND)SendMessageA(client,WM_MDIGETACTIVE,0,(LPARAM)&zoomed);
    trace("MDI_STATE step=%s active=%u zoomed=%u focus=%u frame=%u one=%u/%u/%u two=%u/%u/%u\n",step,tag(active),zoomed,tag(GetFocus()),tag(GetActiveWindow()),
          !!IsWindow(one),!!IsZoomed(one),!!IsIconic(one),!!IsWindow(two),!!IsZoomed(two),!!IsIconic(two));
}
START_TEST(mdi_state)
{
    WNDCLASSA cls;CLIENTCREATESTRUCT c={NULL,100};
    memset(&cls,0,sizeof cls);cls.hInstance=GetModuleHandleA(NULL);cls.lpfnWndProc=frame_proc;cls.lpszClassName="MdiStateFrame";
    ok(RegisterClassA(&cls)!=0,"frame class\n");cls.lpfnWndProc=child_proc;cls.lpszClassName="MdiStateChild";ok(RegisterClassA(&cls)!=0,"child class\n");
    frame=CreateWindowA("MdiStateFrame","frame",WS_OVERLAPPEDWINDOW|WS_VISIBLE,40,40,300,250,NULL,NULL,cls.hInstance,NULL);
    ok(frame!=NULL,"frame\n");
    client=CreateWindowA("MDICLIENT",NULL,WS_CHILD|WS_VISIBLE|MDIS_ALLCHILDSTYLES,0,0,200,160,frame,NULL,cls.hInstance,&c);
    ok(client!=NULL,"client\n");SetFocus(NULL);state("initial");
    one=child(0);state("hidden");ShowWindow(one,SW_SHOW);state("show");DestroyWindow(one);one=NULL;state("destroy-shown");
    one=child(WS_VISIBLE);state("visible");DestroyWindow(one);one=NULL;state("destroy-visible");
    one=child(0);state("hidden-again");ShowWindow(one,SW_MAXIMIZE);state("maximize-hidden");
    ShowWindow(one,SW_RESTORE);state("restore");ShowWindow(one,SW_MINIMIZE);state("minimize");ShowWindow(one,SW_RESTORE);state("restore-minimized");
    DestroyWindow(one);one=NULL;SetFocus(NULL);state("destroy-restored");
    one=child(WS_VISIBLE|WS_MAXIMIZEBOX|WS_MAXIMIZE);state("max-one");
    two=child(WS_VISIBLE|WS_MAXIMIZEBOX|WS_MAXIMIZE);state("max-two");
    DestroyWindow(two);two=NULL;state("destroy-two");
    ShowWindow(one,SW_MAXIMIZE);state("maximize-one");
    two=child(WS_VISIBLE|WS_MAXIMIZEBOX|WS_MAXIMIZE);state("max-two-again");
    SendMessageA(two,WM_SYSCOMMAND,SC_CLOSE,0);state("close-two");
    DestroyWindow(one);one=NULL;two=NULL;state("destroy-last");
    two=CreateWindowA("MdiStateChild","ordinary",WS_CHILD,0,0,10,10,frame,NULL,cls.hInstance,NULL);
    ok(two!=NULL,"ordinary child\n");state("ordinary-reuse");
    DestroyWindow(two);two=NULL;state("ordinary-destroy");
    DestroyWindow(client);client=CreateWindowA("MDICLIENT",NULL,WS_CHILD|WS_VISIBLE|MDIS_ALLCHILDSTYLES,0,0,200,160,frame,NULL,cls.hInstance,&c);
    ok(client!=NULL,"fresh client\n");SetFocus(NULL);
    one=child(0);state("fresh-hidden");ShowWindow(one,SW_MAXIMIZE);state("fresh-maximize");
    DestroyWindow(one);one=NULL;DestroyWindow(client);
    client=CreateWindowA("MDICLIENT",NULL,WS_CHILD|WS_VISIBLE|MDIS_ALLCHILDSTYLES,0,0,200,160,frame,NULL,cls.hInstance,&c);
    ok(client!=NULL,"second fresh client\n");SetFocus(NULL);
    one=child(WS_VISIBLE|WS_MAXIMIZEBOX|WS_MAXIMIZE);state("fresh-create-max");
    two=child(WS_VISIBLE|WS_MAXIMIZEBOX|WS_MAXIMIZE);state("fresh-second-max");
    SendMessageA(client,WM_MDIACTIVATE,(WPARAM)one,0);state("fresh-switch-max");
    ShowWindow(one,SW_NORMAL);state("fresh-normalize");
    SendMessageA(client,WM_MDIACTIVATE,(WPARAM)two,0);state("fresh-switch-normal");
    DestroyWindow(frame);
}
