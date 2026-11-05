/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void InitializeCriticalSection(void *);
API U InitializeCriticalSectionAndSpinCount(void *, U);
API void DeleteCriticalSection(void *);
API void EnterCriticalSection(void *);
API void LeaveCriticalSection(void *);
API U TlsAlloc(void);
API U TlsFree(U);
API U TlsGetValue(U);
API U TlsSetValue(U, U);
API U CreateThread(U, U, U, U, U, U);
API U CreateEventA(U, U, U, U);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U TerminateThread(U, U);
API U GetLastError(void);

API void SetLastError(U);
API void Sleep(U);
API void ExitProcess(U);
static U section[8], other[8], slot, gate;
static volatile U started, entered, active, total;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall holder(U unused)
{
    (void)unused;
    EnterCriticalSection(section + 1);
    active = 98;
    check(SetEvent(gate), 2730);
    Sleep(10);
    active = 0;
    LeaveCriticalSection(section + 1);
    return 0;
}
/* A terminated waiter never resumes to remove itself. Skip it during ownership transfer and release critical sections held by terminated threads, matching Windows 98. */
static void guards(U *s);
static U kill_section[8];
static volatile U kill_blocked, kill_ready;

static U __stdcall kill_waiter(U unused)
{
    (void)unused;
    kill_blocked = 1;
    EnterCriticalSection(kill_section + 1);
    kill_blocked = 2;
    LeaveCriticalSection(kill_section + 1);
    return 0;
}
static U __stdcall kill_holder(U unused)
{
    (void)unused;
    EnterCriticalSection(kill_section + 1);
    kill_ready = 1;
    for (;;)
        Sleep(10);
}
static void terminated_owner_tests(void)
{
    for (U i = 0; i < 8; ++i)
        kill_section[i] = 0xa1b2c3d4;
    InitializeCriticalSection(kill_section + 1);
    EnterCriticalSection(kill_section + 1);
    kill_blocked = 0;
    U victim = CreateThread(0, 0, (U)kill_waiter, 0, 0, 0);
    check(victim != 0, 2740);
    while (kill_blocked != 1)
        Sleep(0);
    for (U i = 0; i < 8; ++i)
        Sleep(0);
    check(kill_blocked == 1, 2741); /* The thread is on the wait list. */
    check(TerminateThread(victim, 1) != 0, 2742);
    LeaveCriticalSection(kill_section + 1);
    EnterCriticalSection(kill_section + 1); /* Do not transfer ownership to the terminated thread. */
    check(kill_blocked == 1, 2743);
    LeaveCriticalSection(kill_section + 1);
    check(CloseHandle(victim), 2744);

    kill_ready = 0;
    U holder = CreateThread(0, 0, (U)kill_holder, 0, 0, 0);
    check(holder != 0, 2745);
    while (!kill_ready)
        Sleep(0);
    check(TerminateThread(holder, 1) != 0, 2746);
    EnterCriticalSection(kill_section + 1); /* The terminated owner releases ownership. */
    LeaveCriticalSection(kill_section + 1);
    check(CloseHandle(holder), 2747);
    DeleteCriticalSection(kill_section + 1);
    check(!(kill_section[1] & 255) && !kill_section[2], 2748);
    guards(kill_section);
}
static void guards(U *s)

{
    check((s[1] & 0xffffff00) == 0xa1b2c300, 2700);
    check(s[0] == 0xa1b2c3d4 && s[7] == 0xa1b2c3d4, 2701);
    for (U i = 3; i < 7; ++i)
        check(s[i] == 0xa1b2c3d4, 2702);
}
static U __stdcall worker(U id)
{
    check(TlsGetValue(slot) == 0, 2703);
    check(TlsSetValue(slot, id), 2704);
    EnterCriticalSection(other + 1);
    ++started;
    LeaveCriticalSection(other + 1);
    for (U i = 0; i < 16; ++i)
    {
        EnterCriticalSection(section + 1);
        EnterCriticalSection(section + 1);
        check(!active, 2705);
        active = id;
        ++entered;
        U previous = total;
        Sleep(0);
        check(active == id && total == previous && TlsGetValue(slot) == id, 2706);
        total = previous + 1;
        active = 0;
        LeaveCriticalSection(section + 1);
        LeaveCriticalSection(section + 1);
        Sleep(0);
    }
    check(!WaitForSingleObject(gate, 10000), 2707);
    check(TlsGetValue(slot) == 0, 2708); /* TlsFree cleared every waiting thread. */
    return id;
}
void run_critical_test(void)
{
    for (U i = 0; i < 8; ++i)
        section[i] = other[i] = 0xa1b2c3d4;
    SetLastError(98);
    InitializeCriticalSection(section + 1);
    check(GetLastError() == 98 && (section[1] & 255) == 4 && section[2], 2709);
    guards(section);
    U old = section[2];
    InitializeCriticalSection(section + 1); /* The Win98 entry releases an existing record. */
    check(section[2] && section[2] != old, 2710);
    check(InitializeCriticalSectionAndSpinCount(other + 1, ~0u) == 0, 2711);
    guards(other);
    gate = CreateEventA(0, 1, 0, 0);
    U holding = CreateThread(0, 0, (U)holder, 0, 0, 0);
    check(gate && holding && !WaitForSingleObject(gate, 10000) && active == 98, 2731);
    EnterCriticalSection(section + 1); /* The main fiber must wait for the sleeping owner. */
    check(!active, 2732);
    LeaveCriticalSection(section + 1);
    check(!WaitForSingleObject(holding, 10000) && CloseHandle(holding) && CloseHandle(gate), 2733);
    EnterCriticalSection(section + 1);
    EnterCriticalSection(section + 1);
    slot = TlsAlloc();
    check(slot < 80 && TlsSetValue(slot, 98), 2712);
    gate = CreateEventA(0, 1, 0, 0);
    U threads[4];
    for (U i = 0; i < 4; ++i)
    {
        threads[i] = CreateThread(0, 0, (U)worker, i + 1, 0, 0);
        check(threads[i], 2713);
    }
    while (started != 4)
        Sleep(0);
    check(!entered, 2714);
    LeaveCriticalSection(section + 1);
    for (U i = 0; i < 8; ++i)
        Sleep(0);
    check(!entered, 2715); /* The recursive owner must release twice. */
    LeaveCriticalSection(section + 1);
    while (total != 64)
        Sleep(0);
    EnterCriticalSection(section + 1); /* Main thread also uses the blocking path. */
    check(!active && TlsGetValue(slot) == 98, 2716);
    LeaveCriticalSection(section + 1);
    check(TlsFree(slot) && TlsAlloc() == slot && !TlsGetValue(slot), 2717);
    check(SetEvent(gate), 2718);
    for (U i = 0; i < 4; ++i)
        check(!WaitForSingleObject(threads[i], 10000) && CloseHandle(threads[i]), 2719);
    check(CloseHandle(gate) && TlsFree(slot), 2720);
    DeleteCriticalSection(section + 1);
    DeleteCriticalSection(other + 1);
    check(!(section[1] & 255) && !section[2] && !(other[1] & 255) && !other[2], 2721);
    guards(section);
    guards(other);
    LeaveCriticalSection(section + 1); /* Invalid tag is ignored by the original Leave entry. */

    /* Original Win98 has 80 dynamic slots, including range-only Get/Set on free slots.
       Slots below the first free index belong to DLLs that took theirs while the
       process loaded them; this program imports COMDLG32, which allocates its own
       index in its DLL_PROCESS_ATTACH at original. The remaining slots
       still come back in ascending order from the single allocation bitmap. */
    U slots[80], first, count;
    SetLastError(98);
    check(TlsSetValue(79, 0x123498) && GetLastError() == 98, 2722);
    first = TlsAlloc();
    check(first < 80, 2723);
    slots[0] = first;
    count = 80 - first;
    for (U i = 1; i < count; ++i)
    {
        slots[i] = TlsAlloc();
        check(slots[i] == first + i, 2723);
    }
    check(TlsGetValue(79) == 0x123498 && !GetLastError(), 2724);
    check(TlsAlloc() == ~0u && GetLastError() == 259, 2725);
    check(!TlsSetValue(80, 1) && GetLastError() == 87, 2726);
    check(!TlsGetValue(~0u) && GetLastError() == 87 && !TlsFree(80), 2727);
    for (U i = 0; i < count; ++i)
        check(TlsFree(slots[i]), 2728);
    check(!TlsFree(first) && GetLastError() == 87, 2729);
    terminated_owner_tests();
}

