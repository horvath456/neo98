/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U InterlockedExchange(volatile U *, U);
API U InterlockedCompareExchange(volatile U *, U, U);
API U InterlockedIncrement(volatile U *);
API U InterlockedDecrement(volatile U *);
API U InterlockedExchangeAdd(volatile U *, U);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void Sleep(U);
API void SetLastError(U);
API U GetLastError(void);
API void ExitProcess(U);
static volatile U increments, decrements, sum, lock, protected_count;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall worker(U unused)
{
    (void)unused;
    for (U i = 0; i < 64; ++i)
    {
        InterlockedIncrement(&increments);
        InterlockedDecrement(&decrements);
        InterlockedExchangeAdd(&sum, 3);
        while (InterlockedCompareExchange(&lock, 1, 0))
            Sleep(0);
        U before = protected_count;
        Sleep(0); /* The other guest fiber runs while this lock is held. */
        protected_count = before + 1;
        check(InterlockedExchange(&lock, 0) == 1, 3450);
    }
    return 0;
}
void run_interlocked_test(void)
{
    volatile U value = 0xffffffff;
    SetLastError(98);
    check(InterlockedIncrement(&value) == 0 && value == 0, 3451);
    check(InterlockedDecrement(&value) == 0xffffffff && value == 0xffffffff, 3452);
    check(InterlockedExchange(&value, 0x7fffffff) == 0xffffffff && value == 0x7fffffff, 3453);
    check(InterlockedIncrement(&value) == 0x80000000 && value == 0x80000000, 3454);
    check(InterlockedDecrement(&value) == 0x7fffffff && value == 0x7fffffff, 3455);
    check(InterlockedCompareExchange(&value, 98, 0x80000000) == 0x7fffffff && value == 0x7fffffff, 3456);
    check(InterlockedCompareExchange(&value, 0xffffffff, 0x7fffffff) == 0x7fffffff && value == 0xffffffff, 3457);
    check(InterlockedExchangeAdd(&value, 2) == 0xffffffff && value == 1, 3458);
    check(InterlockedExchangeAdd(&value, 0xffffffff) == 1 && value == 0, 3459);
    check(GetLastError() == 98, 3460);
    increments = sum = lock = protected_count = 0;
    decrements = 128;
    U first = CreateThread(0, 0, (U)worker, 0, 0, 0);
    U second = CreateThread(0, 0, (U)worker, 0, 0, 0);
    check(first && second, 3461);
    check(!WaitForSingleObject(first, 10000) && !WaitForSingleObject(second, 10000), 3462);
    check(increments == 128 && decrements == 0 && sum == 384 && protected_count == 128 && !lock, 3463);
    check(CloseHandle(first) && CloseHandle(second), 3464);
}
