/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
static DWORD rows[4096], used, failed;
static void check(DWORD tag, DWORD got, DWORD want)
{
    rows[used++] = tag; rows[used++] = got; rows[used++] = want;
    failed += got != want;
}
void mainCRTStartup(void)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "ExcludeUpdate"; RegisterClassA(&wc);
    HWND w = CreateWindowExA(0,wc.lpszClassName,"",WS_POPUP|WS_VISIBLE,20,30,400,300,0,0,wc.hInstance,0);
    HWND other = CreateWindowExA(0,wc.lpszClassName,"",WS_POPUP|WS_VISIBLE,50,70,400,300,0,0,wc.hInstance,0);
    UpdateWindow(w); UpdateWindow(other);
    const unsigned counts[] = {0,1,65,257};
    for (unsigned type=0; type<4; ++type) for (unsigned mapping=0; mapping<2; ++mapping)
    for (unsigned explicit_clip=0;explicit_clip<2;++explicit_clip)
    for (unsigned c=0;c<4;++c) {
        unsigned tag=100+type*200+mapping*80+c*10+explicit_clip*1000;
        ValidateRect(w,0);
        HDC dc=type==0?CreateCompatibleDC(0):GetDC(type==1?w:type==2?other:0);
        HBITMAP bitmap=0, old=0;
        if (!type) { bitmap=CreateBitmap(500,400,1,32,0); old=SelectObject(dc,bitmap); }
        HRGN update=CreateRectRgn(0,0,0,0), full=CreateRectRgn(-100,-100,500,400);
        HRGN actual=CreateRectRgn(0,0,0,0), expected=CreateRectRgn(0,0,0,0);
        for (unsigned i=0;i<counts[c];++i) {
            RECT r={(i%32)*10,(i/32)*10,(i%32)*10+3,(i/32)*10+3};
            HRGN part=CreateRectRgnIndirect(&r);
            CombineRgn(update,update,part,RGN_OR); DeleteObject(part); InvalidateRect(w,&r,FALSE);
        }
        POINT client={0,0}, origin={0,0}; ClientToScreen(w,&client); GetDCOrgEx(dc,&origin);
        OffsetRgn(update,client.x-origin.x,client.y-origin.y);
        if (!explicit_clip) SetRectRgn(full,-origin.x,-origin.y,
            GetSystemMetrics(SM_CXSCREEN)-origin.x,GetSystemMetrics(SM_CYSCREEN)-origin.y);
        CombineRgn(expected,full,update,RGN_DIFF);
        if (explicit_clip) SelectClipRgn(dc,full);
        else if (!counts[c]) SetRectRgn(expected,0,0,0,0);
        if (mapping) {
            SetMapMode(dc,MM_ANISOTROPIC); SetWindowExtEx(dc,3,5,0); SetViewportExtEx(dc,7,-11,0);
            SetWindowOrgEx(dc,13,17,0); SetViewportOrgEx(dc,23,29,0);
        }
        int result=ExcludeUpdateRgn(dc,w); RECT box;
        check(tag,result,GetClipBox(dc,&box));
        check(tag+1,GetClipRgn(dc,actual),explicit_clip||counts[c]?1:0);
        check(tag+2,EqualRgn(actual,expected),1);
        /* Repeating the operation is idempotent and does not validate w. */
        check(tag+3,ExcludeUpdateRgn(dc,w)!=ERROR,1);
        GetClipRgn(dc,actual); check(tag+4,EqualRgn(actual,expected),1);
        check(tag+5,GetUpdateRgn(w,actual,FALSE),counts[c]>1?COMPLEXREGION:counts[c]?SIMPLEREGION:NULLREGION);
        DeleteObject(update); DeleteObject(full); DeleteObject(actual); DeleteObject(expected);
        if (!type) { SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); }
        else ReleaseDC(type==1?w:type==2?other:0,dc);
    }
    DestroyWindow(other); DestroyWindow(w);
    HANDLE f=CreateFileA("C:\\UPDRGN.BIN",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    DWORD written; WriteFile(f,&failed,4,&written,0); WriteFile(f,rows,used*4,&written,0); CloseHandle(f);
    ExitProcess(failed);
}
