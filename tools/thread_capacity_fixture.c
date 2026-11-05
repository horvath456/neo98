/* SPDX-License-Identifier: GPL-2.0-or-later
 * x86 test program for how many threads one process can
 * hold at once, and how much stack a thread gets.
 *
 * Win98 has no small limit on the threads of a process; Internet Explorer 5's
 * setup keeps more than fifteen waiting at once (CRYPT32 starts one for every
 * certificate store it watches) beside the finished install threads whose
 * handles INSENG never closes. Win98 also reads dwStackSize as the size to
 * commit: the stack is the program's SizeOfStackReserve (1 MB here) unless the
 * commit is larger. msiexec starts a custom action thread
 * with 0A000h that probes 160 KB of stack. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U CreateEventA(void *, U, U, const char *);
API U SetEvent(U);
API U CloseHandle(U);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U HeapCreate(U, U, U);
API U HeapDestroy(U);
API void *HeapAlloc(U, U, U);
#define COUNT 24
static U gate;
static volatile U started;
static U __stdcall quick(void *parameter)
{
    return 200 + (U)parameter;
}
static U __stdcall waiter(void *parameter)
{
    ++started;
    WaitForSingleObject(gate, 0xffffffff);
    return 100 + (U)parameter;
}
// Touches the stack page by page down to 160 KB below the caller's frame, as
// the stack probe of a large frame (_chkstk) does.
static U __stdcall deep(void *parameter)
{
    U sum = 0, pages = (U)parameter;
    for (U page = 1; page <= pages; ++page)
    {
        U value;
        __asm__ volatile("movl %%esp,%%eax\n\tsubl %1,%%eax\n\tmovl (%%eax),%0" : "=r"(value) : "r"(page * 4096) : "eax");
        sum += value;
    }
    return 7 + (sum & 0);
}
static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
void mainCRTStartup(void)
{
    U code = 0, id = 0;
    U probe = CreateThread(0, 0xa000, deep, (void *)40, 0, &id);
    check(probe != 0, 1);
    check(WaitForSingleObject(probe, 10000) == 0, 2);
    check(GetExitCodeThread(probe, &code) && code == 7, 3);
    CloseHandle(probe);
    // Finished threads whose handles stay open, as INSENG leaves one for
    // every component it installed.
    U finished[30];
    for (U i = 0; i < 30; ++i)
    {
        finished[i] = CreateThread(0, 0, quick, (void *)i, 0, &id);
        check(finished[i] != 0, 5);
        check(WaitForSingleObject(finished[i], 10000) == 0, 6);
    }
    gate = CreateEventA(0, 1, 0, 0);
    check(gate != 0, 4);
    U threads[COUNT];
    for (U i = 0; i < COUNT; ++i)
    {
        threads[i] = CreateThread(0, 0, waiter, (void *)i, 0, &id);
        check(threads[i] != 0, 10);
    }
    for (U spin = 0; started < COUNT && spin < 100000; ++spin)
        WaitForSingleObject(gate, 1);
    check(started == COUNT, 1000 + started);
    SetEvent(gate);
    for (U i = 0; i < COUNT; ++i)
    {
        check(WaitForSingleObject(threads[i], 10000) == 0, 12);
        check(GetExitCodeThread(threads[i], &code) && code == 100 + i, 13);
        CloseHandle(threads[i]);
    }
    for (U i = 0; i < 30; ++i)
    {
        check(GetExitCodeThread(finished[i], &code) && code == 200 + i, 14);
        CloseHandle(finished[i]);
    }
    /* a heap for each of a hundred DLLs with the static C runtime,
     * as iernonce loads them one after the other. */
    static U heaps[100];
    for (U i = 0; i < 100; ++i)
    {
        heaps[i] = HeapCreate(0, 0x1000, 0);
        check(heaps[i] != 0, 20);
        check(HeapAlloc(heaps[i], 0, 64) != 0, 21);
    }
    for (U i = 0; i < 100; ++i)
        check(HeapDestroy(heaps[i]) != 0, 22);
    ExitProcess(0);
}
