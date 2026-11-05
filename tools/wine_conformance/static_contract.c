/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"
static unsigned calls,phase,kind;
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b){
    if(m==WM_CTLCOLORSTATIC){
        HRGN r=CreateRectRgn(0,0,1,1);int clip=GetClipRgn((HDC)a,r);RECT bounds;
        GetClipBox((HDC)a,&bounds);
        trace("STATIC_COLOR kind=%u phase=%u clip=%d bounds=%ld,%ld,%ld,%ld\n",kind,phase,clip,bounds.left,bounds.top,bounds.right,bounds.bottom);
        ok(clip==1,"static paint clip kind=%u phase=%u\n",kind,phase);DeleteObject(r);++calls;
    }
    return DefWindowProcA(w,m,a,b);
}
START_TEST(static_contract){
    WNDCLASSA wc;HWND parent,child;unsigned n;RECT r,dirty={20,20,30,30};
    static const DWORD styles[]={SS_LEFT,SS_SIMPLE,SS_ICON,SS_BITMAP,SS_BLACKRECT,SS_WHITERECT,SS_ETCHEDHORZ,SS_ETCHEDVERT};
    ZeroMemory(&wc,sizeof wc);wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="StaticContract";wc.hbrBackground=GetStockObject(WHITE_BRUSH);
    ok(RegisterClassA(&wc)!=0,"register parent\n");
    parent=CreateWindowA(wc.lpszClassName,"static test",WS_OVERLAPPEDWINDOW,0,0,500,500,NULL,NULL,wc.hInstance,NULL);
    ok(parent!=NULL,"create parent\n");ShowWindow(parent,SW_SHOW);UpdateWindow(parent);
    for(n=0;n<sizeof styles/sizeof styles[0];++n){
        kind=styles[n];phase=0;calls=0;
        child=CreateWindowA("STATIC","Test",WS_VISIBLE|WS_CHILD|kind,5,5,100,100,parent,(HMENU)1995,NULL,NULL);
        ok(child!=NULL,"create static kind=%u\n",kind);GetWindowRect(child,&r);
        trace("STATIC_SIZE kind=%u width=%ld height=%ld initial_calls=%u\n",kind,r.right-r.left,r.bottom-r.top,calls);calls=0;
        phase=1;InvalidateRect(parent,NULL,FALSE);UpdateWindow(parent);trace("STATIC_PHASE kind=%u phase=%u calls=%u\n",kind,phase,calls);
        phase=2;InvalidateRect(parent,&dirty,FALSE);UpdateWindow(parent);trace("STATIC_PHASE kind=%u phase=%u calls=%u\n",kind,phase,calls);
        phase=3;InvalidateRect(child,&dirty,FALSE);UpdateWindow(child);trace("STATIC_PHASE kind=%u phase=%u calls=%u\n",kind,phase,calls);
        phase=4;InvalidateRect(child,NULL,FALSE);UpdateWindow(child);trace("STATIC_PHASE kind=%u phase=%u calls=%u\n",kind,phase,calls);
        DestroyWindow(child);
    }
    DestroyWindow(parent);
}
