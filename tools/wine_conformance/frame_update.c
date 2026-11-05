/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned paints,frames,erases;
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_PAINT){PAINTSTRUCT ps;++paints;BeginPaint(window,&ps);EndPaint(window,&ps);return 0;}
    if(message==WM_NCPAINT)++frames;
    if(message==WM_ERASEBKGND){++erases;return 1;}
    return DefWindowProcA(window,message,wp,lp);
}
START_TEST(frame_update)
{
    WNDCLASSA cls={0};HWND window;HRGN region;MSG message;unsigned action,i;
    cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleA(NULL);cls.lpszClassName="FrameUpdate";
    ok(RegisterClassA(&cls)!=0,"register\n");
    window=CreateWindowA(cls.lpszClassName,"Frame update",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,200,200,NULL,NULL,cls.hInstance,NULL);
    ok(window!=NULL,"create\n");if(!window)return;
    UpdateWindow(window);
    for(i=0;i<1000&&PeekMessageA(&message,NULL,0,0,PM_REMOVE);++i)DispatchMessageA(&message);
    region=CreateRectRgn(0,0,0,0);ok(region!=NULL,"region\n");
    for(action=0;action<7;++action){
        RECT rect={99,99,99,99};int has,kind,queued,after;unsigned beforeframes;
        RedrawWindow(window,NULL,NULL,RDW_VALIDATE|RDW_NOFRAME|RDW_NOERASE|RDW_NOCHILDREN);
        paints=frames=erases=0;
        SetRectRgn(region,-4,-4,-1,-1);
        ok(RedrawWindow(window,NULL,region,RDW_INVALIDATE|RDW_FRAME),"invalidate frame\n");
        has=GetUpdateRect(window,&rect,FALSE);
        kind=GetUpdateRgn(window,region,FALSE);
        queued=PeekMessageA(&message,window,WM_PAINT,WM_PAINT,PM_NOREMOVE);
        trace("FRAME_UPDATE before action=%u rect=%d,%ld,%ld,%ld,%ld region=%d paint=%d frames=%u erases=%u\n",action,has,rect.left,rect.top,rect.right,rect.bottom,kind,queued,frames,erases);
        if(action==0)after=GetUpdateRect(window,&rect,TRUE);
        else if(action==1)after=GetUpdateRgn(window,region,TRUE);
        else if(action==2)after=RedrawWindow(window,NULL,NULL,RDW_ERASENOW);
        else if(action==3){SetRectRgn(region,-200,-200,-198,-198);after=RedrawWindow(window,NULL,region,RDW_VALIDATE|RDW_NOFRAME);}
        else if(action==4){SetRectRgn(region,-4,-4,-3,-3);after=RedrawWindow(window,NULL,region,RDW_VALIDATE|RDW_NOFRAME);}
        else if(action==5)after=UpdateWindow(window);
        else {PAINTSTRUCT ps;after=BeginPaint(window,&ps)!=NULL;EndPaint(window,&ps);}
        beforeframes=frames;
        has=GetUpdateRect(window,&rect,FALSE);kind=GetUpdateRgn(window,region,FALSE);
        queued=PeekMessageA(&message,window,WM_PAINT,WM_PAINT,PM_REMOVE);
        if(queued)DispatchMessageA(&message);
        trace("FRAME_UPDATE after action=%u result=%d rect=%d,%ld,%ld,%ld,%ld region=%d paint=%d paints=%u frames=%u,%u erases=%u\n",action,after,has,rect.left,rect.top,rect.right,rect.bottom,kind,queued,paints,beforeframes,frames,erases);
    }
    DeleteObject(region);DestroyWindow(window);
}
