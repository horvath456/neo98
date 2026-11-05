/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise native named objects from actual x86 threads, including abandoned ownership. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateMutexA(void *, U, const char *);
API U OpenMutexA(U, U, const char *);
API U ReleaseMutex(U);
API U CreateEventA(void *, U, U, const char *);
API U OpenEventA(U, U, const char *);
API U SetEvent(U);
API U ResetEvent(U);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U GetLastError(void);
API void SetLastError(U);
API void Sleep(U);
API void ExitThread(U);
API void ExitProcess(U);
static U mutex, gate;
static volatile U entered, active, total, started;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall worker(U id)
{
    U h = CreateMutexA(0, 1, "neo98-mutex");
    check(h && GetLastError() == 183, 2900);
    check(!ReleaseMutex(h) && GetLastError() == 288, 2901);
    check(WaitForSingleObject(h, 1) == 258, 2902);
    ++started;
    for (U i = 0; i < 12; ++i)
    {
        check(!WaitForSingleObject(h, 10000), 2903);
        check(!WaitForSingleObject(h, 0), 2904);
        check(!active, 2905);
        active = id;
        ++entered;
        U previous = total;
        Sleep(0);
        check(active == id && total == previous, 2906);
        check(ReleaseMutex(h), 2907);
        Sleep(0);
        check(active == id && total == previous, 2908);
        total = previous + 1;
        active = 0;
        check(ReleaseMutex(h), 2909);
        Sleep(0);
    }
    check(CloseHandle(h), 2910);
    return id;
}
static U __stdcall abandons(U explicit_exit)
{
    check(!WaitForSingleObject(mutex, 10000) && !WaitForSingleObject(mutex, 0), 2911);
    check(SetEvent(gate), 2912);
    Sleep(10); /* Main thread must block before this owner disappears. */
    if (explicit_exit)
        ExitThread(98);
    return 98;
}
void run_mutex_test(void)
{
    U attributes[] = {12, 0, 1};
    mutex = CreateMutexA(attributes, 1, "neo98-mutex");
    check(mutex && !GetLastError(), 2913);
    U h = OpenMutexA(0x100000, 0, "neo98-mutex");
    check(h && h != mutex && !WaitForSingleObject(h, 0), 2914);
    check(ReleaseMutex(h) && CloseHandle(h), 2915); /* Release needs ownership, not MODIFY_STATE. */
    /* The name search compares case-sensitively, and an absent name is
        ERROR_INVALID_NAME: Windows 98 leaves with -1 and OpenMutexA Windows 98 answers 7Bh, the same value the implemented OpenSemaphoreA
        and OpenFileMappingA already use. */
    check(!OpenMutexA(0x1f0001, 0, "NEO98-MUTEX") && GetLastError() == 123, 2916);
    check(!CreateEventA(0, 0, 0, "neo98-mutex") && GetLastError() == 6, 2917);
    check(!OpenEventA(0x1f0003, 0, "neo98-mutex") && GetLastError() == 6, 2918);
    U threads[3];
    for (U i = 0; i < 3; ++i)
    {
        threads[i] = CreateThread(0, 0, (U)worker, i + 1, 0, 0);
        check(threads[i], 2919);
    }
    while (started != 3)
        Sleep(0);
    check(!entered && ReleaseMutex(mutex), 2920);
    for (U i = 0; i < 3; ++i)
        check(!WaitForSingleObject(threads[i], 10000) && CloseHandle(threads[i]), 2921);
    check(total == 36 && entered == 36 && !active, 2922);
    gate = CreateEventA(attributes, 0, 0, "neo98-ready");
    check(gate, 2923);
    for (U i = 0; i < 2; ++i)
    {
        U thread = CreateThread(0, 0, (U)abandons, i, 0, 0);
        check(thread && !WaitForSingleObject(gate, 10000), 2924);
        check(WaitForSingleObject(mutex, 10000) == 0x80, 2925);
        check(!WaitForSingleObject(mutex, 0) && ReleaseMutex(mutex) && ReleaseMutex(mutex), 2926);
        check(!ReleaseMutex(mutex) && GetLastError() == 288, 2927);
        check(!WaitForSingleObject(thread, 10000) && CloseHandle(thread), 2928);
    }
    h = OpenEventA(0x100000, 0, "neo98-ready");
    U write = OpenEventA(2, 1, "neo98-ready");
    check(h && write, 2929);
    check(!SetEvent(h) && GetLastError() == 5 && !ResetEvent(h) && GetLastError() == 5, 2930);
    /* Waits look the handle up with required access 0 (KERNEL32), so a handle without SYNCHRONIZE is waitable. */
    check(WaitForSingleObject(write, 0) == 258, 2931);
    check(SetEvent(write) && !WaitForSingleObject(h, 0) && WaitForSingleObject(gate, 0) == 258, 2932);
    check(!OpenMutexA(0x100000, 0, "neo98-ready") && GetLastError() == 6, 2933);
    check(!CreateMutexA(0, 0, "neo98-ready") && GetLastError() == 6, 2934);
    check(CloseHandle(h) && CloseHandle(write) && CloseHandle(gate), 2935);
    check(!OpenEventA(0x100000, 0, "neo98-ready") && GetLastError() == 123, 2936);  /* */
    check(!OpenEventA(0, 0, 0) && GetLastError() == 87, 2937);
    check(!OpenMutexA(0, 0, 0) && GetLastError() == 87, 2938);
    h = OpenMutexA(0, 0, "neo98-mutex");
    check(h && WaitForSingleObject(h, 0) == 0 && ReleaseMutex(h), 2939);   /* access 0 is enough to wait */
    check(CloseHandle(h) && CloseHandle(mutex), 2940);
    check(!OpenMutexA(0x100000, 0, "neo98-mutex") && GetLastError() == 123, 2941);  /* */
    h = CreateMutexA(0, 0, "neo98-mutex");
    check(h && h != mutex && !GetLastError(), 2942);
    check(!ReleaseMutex(mutex) && GetLastError() == 6, 2943);
    check(!WaitForSingleObject(h, 0) && ReleaseMutex(h) && CloseHandle(h), 2944);
    U unnamed[2] = {CreateMutexA(0, 1, 0), CreateMutexA(0, 0, 0)};
    check(unnamed[0] && unnamed[1] && !ReleaseMutex(unnamed[1]) && GetLastError() == 288, 2945);
    check(ReleaseMutex(unnamed[0]) && CloseHandle(unnamed[0]) && CloseHandle(unnamed[1]), 2946);
    /* Every reference slot can be reused after the last close. */
    for (U i = 0; i < 140; ++i)
    {
        h = CreateMutexA(attributes, 1, "neo98-recycle");
        check(h && !GetLastError() && ReleaseMutex(h) && CloseHandle(h), 2947);
    }
    attributes[0] = 8;
    check(!CreateMutexA(attributes, 0, 0) && GetLastError() == 87, 2948);
    check(!CreateEventA(attributes, 0, 0, 0) && GetLastError() == 87, 2949);
}
