/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateThread(void *, U, U (__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API void Sleep(U);
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U TlsSetValue(U, void *);
API void *TlsGetValue(U);
static volatile U started, stop, rounds;
static U __stdcall worker(void *unused)
{
    (void)unused;
    if (!TlsSetValue(79, (void *)1234)) ExitProcess(2);
    started = 1;
    while (!stop) {
        SetLastError(88);
        if (GetLastError() != 88) ExitProcess(1);
        if (TlsGetValue(79) != (void *)1234 || GetLastError()) ExitProcess(3);
        ++rounds;
    }
    return 0;
}
void mainCRTStartup(void)
{
    SetLastError(99);
    TlsSetValue(79, (void *)5678);
    /* Warm every direct entry before the worker publishes started, so its
       loop cannot yield through the first-call logging gates. */
    if (GetLastError() != 99 || TlsGetValue(79) != (void *)5678) ExitProcess(7);
    U thread = CreateThread(0, 0, worker, 0, 0, 0);
    if (!thread) ExitProcess(4);
    while (!started) Sleep(0);
    /* Reaching here requires preemption of the worker's API-only loop. */
    stop = 1;
    if (WaitForSingleObject(thread, 5000) || !rounds) ExitProcess(5);
    if (TlsGetValue(79) != (void *)5678) ExitProcess(6);
    ExitProcess(0);
}
