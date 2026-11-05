/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real x86 PE test program: threads, events, TLS state and x87 preservation. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U CreateEventA(void *, U, U, const char *);
API U SetEvent(U);
API U ResetEvent(U);
API U CloseHandle(U);
API U WaitForSingleObject(U, U);
API U ResumeThread(U);
API U GetExitCodeThread(U, U *);
API U GetCurrentThreadId(void);
API U GetLastError(void);
API void SetLastError(U);
API void Sleep(U);
API U GetTickCount(void);
static U gate, done[2], ids[2];
static volatile U visits[2], tebs[2];
static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
static U __stdcall worker(void *parameter)
{
    U index = (U)parameter, teb = 0;
    __asm__ volatile("movl %%fs:0x18,%0" : "=r"(teb));
    tebs[index] = teb;
    check(GetCurrentThreadId() == ids[index], 10 + index);
    check(WaitForSingleObject(gate, 0xffffffff) == 0, 12);
    U tag = 0x98760000 + index;
    SetLastError(tag);
    unsigned short cw = 0x37f | (index ? 0x400 : 0x800), actual;
    __asm__ volatile("fldcw %0" ::"m"(cw));
    for (U step = 0; step < 80; ++step)
    {
        volatile U local[31];
        for (U n = 0; n < 31; ++n)
            local[n] = step * 313 + index + n;
        Sleep(0);
        check(GetLastError() == tag, 14);
        __asm__ volatile("fnstcw %0" : "=m"(actual));
        check(actual == cw, 15);
        U now;
        __asm__ volatile("movl %%fs:0x18,%0" : "=r"(now));
        check(now == teb, 16);
        for (U n = 0; n < 31; ++n)
            check(local[n] == step * 313 + index + n, 17);
        ++visits[index];
    }
    check(SetEvent(done[index]), 18);
    return 98 + index;
}
void mainCRTStartup(void)
{
    check(GetCurrentThreadId() == 1, 1);
    gate = CreateEventA(0, 1, 0, "neo98-start");
    check(gate, 2);
    U same = CreateEventA(0, 0, 1, "neo98-start");
    check(same && GetLastError() == 183 && same != gate, 3);
    check(WaitForSingleObject(same, 0) == 258, 4);
    check(CloseHandle(same), 5);
    U threads[2];
    for (U n = 0; n < 2; ++n)
    {
        done[n] = CreateEventA(0, 0, 0, 0);
        check(done[n], 6);
        threads[n] = CreateThread(0, 0, worker, (void *)n, 4, &ids[n]);
        check(threads[n], 7);
    }
    check(ids[0] != ids[1], 8);
    check(!visits[0] && !visits[1], 9);
    for (U n = 0; n < 2; ++n)
        check(ResumeThread(threads[n]) == 1, 20);
    check(SetEvent(gate), 21);
    for (U n = 0; n < 2; ++n)
    {
        check(WaitForSingleObject(done[n], 0xffffffff) == 0, 22);
        check(WaitForSingleObject(done[n], 0) == 258, 23);
        check(WaitForSingleObject(threads[n], 0xffffffff) == 0, 24);
        U code;
        check(GetExitCodeThread(threads[n], &code) && code == 98 + n, 25);
        check(CloseHandle(threads[n]), 26);
    }
    check(visits[0] == 80 && visits[1] == 80 && tebs[0] != tebs[1], 27);
    U start = GetTickCount();
    check(WaitForSingleObject(done[0], 3) == 258, 28);
    check(GetTickCount() - start >= 2, 29);
    check(CloseHandle(done[0]) && CloseHandle(done[1]) && CloseHandle(gate), 30);
    check(!SetEvent(gate) && GetLastError() == 6, 31);
    ExitProcess(0);
}
