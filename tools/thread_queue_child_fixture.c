/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The child of tools/thread_queue_fixture.c: shows one window and pumps until
   the launcher ends it. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API void Sleep(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U ShowWindow(U, int);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);

static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
void mainCRTStartup(void)
{
    U wc[10] = {0};
    wc[1] = (U) procedure;
    wc[4] = GetModuleHandleA(0);
    wc[9] = (U) "QueueChild";
    RegisterClassA(wc);
    const U w = CreateWindowExA(0, "QueueChild", "child", 0x00cf0000, 300, 200, 200, 150, 0, 0, GetModuleHandleA(0), 0);
    ShowWindow(w, 1);
    U msg[7];
    for (U i = 0; i < 3000; ++i)
    {
        while (PeekMessageA(msg, 0, 0, 0, 1))
            DispatchMessageA(msg);
        Sleep(10);
    }
    ExitProcess(0);
}
