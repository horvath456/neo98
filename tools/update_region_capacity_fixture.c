/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static DWORD rows[2048], used, failed, erases;
static void check(DWORD tag, DWORD got, DWORD want)
{
    rows[used++] = tag; rows[used++] = got; rows[used++] = want;
    if (got != want) ++failed;
}
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM b)
{
    if (m == WM_ERASEBKGND) { ++erases; return 1; }
    return DefWindowProcA(w,m,a,b);
}
void mainCRTStartup(void)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "LargeUpdate";
    RegisterClassA(&wc);
    HWND w = CreateWindowExA(0,wc.lpszClassName,"",WS_POPUP|WS_VISIBLE,20,20,400,300,0,0,wc.hInstance,0);
    check(1,w != 0,1);
    UpdateWindow(w);
    const unsigned counts[] = {0,1,64,65,257};
    for (unsigned c=0;c<sizeof(counts)/sizeof(counts[0]);++c) {
        unsigned n=counts[c], tag=100+c*20;
        ValidateRect(w,0);
        HRGN expected=CreateRectRgn(0,0,0,0), actual=CreateRectRgn(0,0,1,1);
        for (unsigned i=0;i<n;++i) {
            int x=(i%32)*10, y=(i/32)*10;
            RECT r={x,y,x+3,y+3};
            HRGN piece=CreateRectRgnIndirect(&r);
            CombineRgn(expected,expected,piece,RGN_OR);
            InvalidateRect(w,&r,TRUE); DeleteObject(piece);
        }
        check(tag,GetUpdateRgn(w,actual,FALSE),n>1?COMPLEXREGION:n?SIMPLEREGION:NULLREGION);
        check(tag+1,EqualRgn(actual,expected),1);
        DWORD before=erases;
        check(tag+2,GetUpdateRgn(w,actual,TRUE),n>1?COMPLEXREGION:n?SIMPLEREGION:NULLREGION);
        check(tag+3,EqualRgn(actual,expected),1);
        check(tag+4,erases-before,n?1:0);
        HDC dc=GetDC(w);
        HRGN full=CreateRectRgn(0,0,400,300), clip=CreateRectRgn(0,0,0,0);
        SelectClipRgn(dc,full);
        check(tag+5,ExcludeUpdateRgn(dc,w),n?COMPLEXREGION:SIMPLEREGION);
        CombineRgn(full,full,expected,RGN_DIFF);
        check(tag+6,GetClipRgn(dc,clip),1);
        check(tag+7,EqualRgn(full,clip),1);
        ValidateRgn(w,expected);
        check(tag+8,GetUpdateRgn(w,actual,FALSE),NULLREGION);
        DeleteObject(full); DeleteObject(clip); ReleaseDC(w,dc);
        DeleteObject(expected); DeleteObject(actual);
    }
    /* The visibility readers shared the same fixed-size buffer. */
    HWND children[80];
    for (unsigned i=0;i<80;++i)
        children[i]=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE,(i%32)*10,(i/32)*10,3,3,w,0,wc.hInstance,0);
    HDC dc=GetDCEx(w,0,DCX_CACHE|DCX_CLIPCHILDREN);
    unsigned visible=0;
    for (unsigned i=0;i<80;++i) visible+=!!PtVisible(dc,(i%32)*10+1,(i/32)*10+1);
    check(300,dc!=0,1); check(301,visible,0); check(302,!!PtVisible(dc,5,5),1); ReleaseDC(w,dc);
    for (unsigned i=0;i<80;++i) DestroyWindow(children[i]);
    HWND back=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE,0,0,400,300,w,0,wc.hInstance,0);
    for (unsigned i=0;i<80;++i)
        children[i]=CreateWindowExA(0,"STATIC","",WS_CHILD|WS_VISIBLE,(i%32)*10,(i/32)*10,3,3,w,0,wc.hInstance,0);
    SetWindowPos(back,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    dc=GetDCEx(back,0,DCX_CACHE|DCX_CLIPSIBLINGS);
    visible=0;
    for (unsigned i=0;i<80;++i) visible+=!!PtVisible(dc,(i%32)*10+1,(i/32)*10+1);
    check(310,dc!=0,1); check(311,visible,0); check(312,!!PtVisible(dc,5,5),1); ReleaseDC(back,dc);
    DestroyWindow(w);
    HANDLE f=CreateFileA("C:\\UPDRGN.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    DWORD written; WriteFile(f,&failed,4,&written,0); WriteFile(f,rows,used*4,&written,0); CloseHandle(f);
    ExitProcess(failed);
}
