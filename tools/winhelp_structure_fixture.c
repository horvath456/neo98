/* SPDX-License-Identifier: GPL-2.0-or-later */
/* HELP_MULTIKEY structure transport without a C++ standard library. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(U *);
API U CreateWindowExA(U,const char *,const char *,U,int,int,int,int,U,U,U,U);
API U DefWindowProcA(U,U,U,U);
API U DestroyWindow(U);
API U WinHelpA(U,const char *,U,U);
static unsigned char payload[65500];
static U expected, calls;
static void check(int ok,U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U hwnd,U message,U w,U l) {
    if (message == 0x38) {
        const unsigned char *p = (const unsigned char *)l;
        U size = p[0] | p[1] << 8, offset = p[14] | p[15] << 8;
        check((p[2] | p[3] << 8) == 0x201, 570101);
        check(size == expected + 25 && offset == 25 && p[12] == 16, 570102);
        for (U i = 0; i < 9; ++i) check(p[16+i] == (unsigned char)"test.hlp"[i], 570103);
        for (U i = 0; i < expected; ++i) check(p[offset+i] == payload[i], 570104);
        ++calls;
        return 1;
    }
    return DefWindowProcA(hwnd,message,w,l);
}
void mainCRTStartup(void) {
    U cls[10] = {0,(U)proc,0,0,0,0,0,0,0,(U)"MS_WINHELP"};
    check(RegisterClassA(cls),570105);
    U hwnd = CreateWindowExA(0,"MS_WINHELP","structure",0,0,0,100,100,0,0,0,0);
    check(hwnd,570106);
    const U sizes[] = {24,4097,sizeof payload};
    for (U n = 0; n < 3; ++n) {
        expected = sizes[n];
        for (U i = 0; i < expected; ++i) payload[i] = (unsigned char)(i*37+n);
        *(U *)payload = expected;
        check(WinHelpA(hwnd,"test.hlp",0x201,(U)payload),570107);
        check(calls == n+1,570108);
    }
    check(DestroyWindow(hwnd),570109);
    ExitProcess(0);
}
