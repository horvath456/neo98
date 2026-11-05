/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest fixture for USER32!PostThreadMessageA and USER32!RedrawWindow. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U RedrawWindow(U, const int *, U, U);
API U InvalidateRect(U, const int *, U);
API U GetUpdateRect(U, int *, U);
API U PeekMessageA(U *, U, U, U, U);
API U PostThreadMessageA(U, U, U, U);
API U PostThreadMessageW(U, U, U, U);
API U GetCurrentThreadId(void);
API U CreateThread(U, U, U, U, U, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void Sleep(U);
API U GetLastError(void);
API void ExitProcess(U);

#define RDW_INVALIDATE 0x0001
#define RDW_ERASE 0x0004
#define RDW_VALIDATE 0x0008
#define RDW_NOERASE 0x0020
#define RDW_NOCHILDREN 0x0040
#define RDW_ALLCHILDREN 0x0080
#define RDW_UPDATENOW 0x0100

static U parent, child, paints[2], erases[2];
static U worker_ready, worker_seen, worker_wparam, worker_lparam, worker_hwnd = 0xffffffff;

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}
static U slot(U h) { return h == child ? 1u : 0u; }

static U __stdcall proc(U h, U m, U w, U l)
{
    if (m == 0x000f) { ++paints[slot(h)]; }
    if (m == 0x0014) { ++erases[slot(h)]; return 1; }
    return DefWindowProcA(h, m, w, l);
}

static void drain(void)
{
    /* DefWindowProcA validates on WM_PAINT, so UpdateWindow settles both. */
    UpdateWindow(parent);
    UpdateWindow(child);
    paints[0] = paints[1] = erases[0] = erases[1] = 0;
}

static U __stdcall worker(U parameter)
{
    U message[7], spins = 0;
    (void)parameter;
    worker_ready = 1;
    while (spins < 20000)
    {
        if (PeekMessageA(message, 0, 0, 0, 1))
        {
            if (message[1] == 0x8000)
            {
                worker_hwnd = message[0];
                worker_wparam = message[2];
                worker_lparam = message[3];
                worker_seen = 1;
                return 0;
            }
        }
        ++spins;
        Sleep(0);
    }
    return 1;
}

void run_user_post_redraw_test(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U) "RedrawPort"};
    U thread_id = 0, handle, spins;
    int rect[4];
    check(RegisterClassA(cls), 9500);
    parent = CreateWindowExA(0, "RedrawPort", "parent", 0x10000000, 30, 40, 240, 180, 0, 0, 0, 0);
    check(parent != 0, 9501);
    child = CreateWindowExA(0, "RedrawPort", "child", 0x50000000, 10, 10, 90, 60, parent, 0, 0, 0);
    check(child != 0, 9502);
    ShowWindow(parent, 5);
    ShowWindow(child, 5);
    drain();

    /* RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW paints both. */
    check(RedrawWindow(parent, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW), 9503);
    check(paints[0] == 1 && paints[1] == 1, 9504);
    check(erases[0] == 1 && erases[1] == 1, 9505);

    /* RDW_NOCHILDREN keeps the child out of the walk and out of the update. */
    drain();
    check(RedrawWindow(parent, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN | RDW_UPDATENOW), 9506);
    check(paints[0] == 1 && paints[1] == 0, 9507);

    /* A rectangle argument reaches the update region; RDW_VALIDATE removes it. */
    drain();
    rect[0] = 5; rect[1] = 5; rect[2] = 40; rect[3] = 30;
    check(RedrawWindow(parent, rect, 0, RDW_INVALIDATE | RDW_ERASE), 9508);
    check(GetUpdateRect(parent, 0, 0), 9509);
    check(paints[0] == 0, 9510);
    check(RedrawWindow(parent, 0, 0, RDW_VALIDATE | RDW_NOERASE | RDW_NOCHILDREN), 9511);
    check(!GetUpdateRect(parent, 0, 0), 9512);

    /* InvalidateRect with a null window is
       RedrawWindow(desktop, NULL, NULL, RDW_INVALIDATE | RDW_ERASE |
       RDW_ALLCHILDREN | RDW_ERASENOW): the rect and the erase argument are
       dropped, every window is invalid as a whole and erased before the call
       returns, and WM_PAINT follows later. */
    drain();
    rect[0] = 5; rect[1] = 5; rect[2] = 6; rect[3] = 6;
    check(InvalidateRect(0, rect, 0), 9530);
    check(erases[0] == 1 && erases[1] == 1, 9531);
    check(paints[0] == 0 && paints[1] == 0, 9532);
    check(GetUpdateRect(parent, rect, 0) && rect[0] == 0 && rect[1] == 0 && rect[2] > 6 && rect[3] > 6, 9533);
    check(GetUpdateRect(child, 0, 0), 9534);
    drain();

    /* Windows 98 rejects any flag above 0FFFh. */
    check(!RedrawWindow(parent, 0, 0, 0x8000), 9513);

    /* PostThreadMessage: a thread without a queue is refused. */
    check(!PostThreadMessageA(0x4321, 0x8000, 1, 2), 9514);
    check(GetLastError() == 1444, 9515);
    /* The wide entry is the Win9x stub. */
    check(!PostThreadMessageW(GetCurrentThreadId(), 0x8000, 1, 2), 9516);
    check(GetLastError() == 120, 9517);

    handle = CreateThread(0, 0, (U)worker, 0, 0, &thread_id);
    check(handle != 0 && thread_id != 0, 9518);
    for (spins = 0; spins < 20000 && !worker_ready; ++spins)
        Sleep(0);
    check(worker_ready, 9519);
    check(PostThreadMessageA(thread_id, 0x8000, 0x1234, 0x5678), 9520);
    check(WaitForSingleObject(handle, 5000) == 0, 9521);
    check(worker_seen, 9522);
    check(worker_hwnd == 0, 9523);
    check(worker_wparam == 0x1234 && worker_lparam == 0x5678, 9524);
    CloseHandle(handle);

    check(DestroyWindow(parent), 9525);
}

#ifdef USER_POST_REDRAW_STANDALONE
void mainCRTStartup(void) { run_user_post_redraw_test(); ExitProcess(0); }
#endif
