/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U SetTimer(U, U, U, U);
API U KillTimer(U, U);
API U GetMessageA(U *, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(U *);
API U PostMessageA(U, U, U, U);
static U callbacks, timer;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void __stdcall callback(U hwnd, U message, U id, U time)
{
    check(!hwnd && message == 0x113 && id == timer && time, 351);
    ++callbacks;
    check(KillTimer(0, id), 352);
    Sleep(0);
}
void run_timer_test(void)
{
    U msg[7] = {}, again[7] = {};
    timer = SetTimer(0, 0, 10, 0);
    check(timer, 353);
    check(PostMessageA(0, 0x8000, 98, 0), 354);
    // Win98's system timer runs a USER timer at most every 55 ms.
    Sleep(70);
    check(GetMessageA(msg, 0, 0, 0) && msg[1] == 0x8000 && msg[2] == 98, 355);
    check(PeekMessageA(msg, 0, 0x113, 0x113, 0) && msg[1] == 0x113 && msg[2] == timer && !msg[3], 356);
    check(KillTimer(0, timer), 357);
    check(PeekMessageA(again, 0, 0x113, 0x113, 1) && again[2] == timer && again[4] == msg[4], 358);
    check(!PeekMessageA(msg, 0, 0x113, 0x113, 1) && !KillTimer(0, timer), 359);
    timer = SetTimer(0, 0, 10, (U)callback);
    check(timer, 360);
    check(GetMessageA(msg, 0, 0, 0) && msg[1] == 0x113 && msg[3] == (U)callback && callbacks == 0, 361);
    DispatchMessageA(msg);
    check(callbacks == 1, 362);
    Sleep(20);
    check(!PeekMessageA(msg, 0, 0x113, 0x113, 1), 363);
}
