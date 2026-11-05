/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Critical sections under contention, for the guest code of
 * EnterCriticalSection and LeaveCriticalSection.
 *
 * Four threads take one section, two levels deep, and do their work inside it
 * without a single API call, so the time slice of the port (SliceBlocks) ends
 * at every kind of place: inside the section, between two sections, right in
 * front of the gate. A thread that finds the section taken waits in the host,
 * and the owner then has to wake it from LeaveCriticalSection. The work inside
 * is a read, a long computation and a write of a shared counter; a second
 * owner inside the section would lose an increment or see the marker of
 * another thread. The length of the computation changes with every round so
 * that the end of a slice lands on a different instruction each time.
 *
 * Failures exit with 32900 and up. The runner tools/test_critical_guest.cpp
 * reads the counters at `Results`. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void InitializeCriticalSection(void *);
API void DeleteCriticalSection(void *);
API void EnterCriticalSection(void *);
API void LeaveCriticalSection(void *);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U GetCurrentThreadId(void);
API U GetLastError(void);
API void SetLastError(U);
API void Sleep(U);
API void ExitProcess(U);
API void *VirtualAlloc(void *, U, U, U);

enum { Threads = 4, Rounds = 20000 };
static U section[8], quiet[8];
static volatile U owner, counter, spins, collisions;
static volatile U done[Threads + 1];

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}

/* The client of Win98: tag 4 in the low byte of the first dword, the record at
 * +4. The record: recursion at +4, owner at +8, the available counter at +10h. */
static volatile U *record(U *client)
{
    return (volatile U *)client[1];
}

static U work(U seed, U length)
{
    U value = seed;
    for (U i = 0; i < length; ++i)
        value = value * 1103515245u + 12345u + (value >> 7);
    return value;
}

static void round_of(U id, U round)
{
    EnterCriticalSection(section + 1);
    EnterCriticalSection(section + 1);
    if (owner)
        ++collisions;
    owner = id;
    U before = counter;
    spins += work(before + id, 20 + (round * 7 + id * 13) % 61) & 1;
    if (owner != id)
        ++collisions;
    counter = before + 1;
    owner = 0;
    LeaveCriticalSection(section + 1);
    LeaveCriticalSection(section + 1);
    spins += work(round, (round * 5 + id) % 23) & 1;
}

static U __stdcall worker(U id)
{
    for (U round = 0; round < Rounds; ++round)
        round_of(id, round);
    done[id] = 1;
    return id;
}

/* The second phase aims at the guest code itself. The main thread does nothing
 * but enter and leave a free section, so the end of its time slice falls on
 * the guest code of EnterCriticalSection most of the time, and sometimes
 * between its test of the record and its change. The other thread computes
 * outside, enters, and computes a long time inside, so when the main thread
 * resumes, that thread is usually inside. If a slice could end in the middle
 * of the guest code, the main thread would take the section a second time. */
enum { LongRounds = 300 };
static U long_section[8];
static volatile U long_owner, long_done, long_collisions, long_rounds;
static U __stdcall long_worker(U unused)
{
    (void)unused;
    for (U round = 0; round < LongRounds; ++round)
    {
        spins += work(round, 3000 + round % 17) & 1;
        EnterCriticalSection(long_section + 1);
        if (long_owner)
            ++long_collisions;
        long_owner = 2;
        spins += work(round, 20000 + round % 29) & 1;
        if (long_owner != 2)
            ++long_collisions;
        long_owner = 0;
        LeaveCriticalSection(long_section + 1);
    }
    long_done = 1;
    return 0;
}
static void long_phase(void)
{
    InitializeCriticalSection(long_section + 1);
    U handle = CreateThread(0, 0, (U)long_worker, 0, 0, 0);
    check(handle != 0, 32920);
    while (!long_done)
    {
        EnterCriticalSection(long_section + 1);
        if (long_owner)
            ++long_collisions;
        long_owner = 1;
        ++long_rounds;
        if (long_owner != 1)
            ++long_collisions;
        long_owner = 0;
        LeaveCriticalSection(long_section + 1);
    }
    check(WaitForSingleObject(handle, 60000) == 0 && CloseHandle(handle), 32921);
    check(!long_collisions, 32922);
    DeleteCriticalSection(long_section + 1);
}

void mainCRTStartup(void)
{
    InitializeCriticalSection(section + 1);
    InitializeCriticalSection(quiet + 1);
    check((section[1] & 255) == 4 && section[2], 32900);

    /* The record lies in guest memory and says what the original says about a
     * section this thread holds twice: recursion 2, this thread, and a counter
     * that went from 1 down by one for every entry. */
    SetLastError(0x5a5a);
    EnterCriticalSection(section + 1);
    EnterCriticalSection(section + 1);
    volatile U *r = record(section + 1);
    check(r[1] == 2 && r[2] == GetCurrentThreadId() && r[4] == (U)-1, 32901);
    LeaveCriticalSection(section + 1);
    check(r[1] == 1 && r[2] == GetCurrentThreadId() && r[4] == 0, 32902);
    LeaveCriticalSection(section + 1);
    check(r[1] == 0 && r[2] == 0 && r[4] == 1 && GetLastError() == 0x5a5a, 32903);

    /* Three threads against the main thread. */
    U handles[Threads];
    for (U i = 1; i < Threads; ++i)
    {
        handles[i] = CreateThread(0, 0, (U)worker, i, 0, 0);
        check(handles[i] != 0, 32904);
    }
    for (U round = 0; round < Rounds; ++round)
        round_of(Threads, round);
    for (U i = 1; i < Threads; ++i)
    {
        check(WaitForSingleObject(handles[i], 60000) == 0, 32905);
        check(CloseHandle(handles[i]), 32906);
        check(done[i], 32907);
    }
    check(!collisions, 32908);
    check(counter == Threads * Rounds, 32909);
    /* Nobody holds the section, nobody waits. */
    check(r[1] == 0 && r[2] == 0 && r[4] == 1, 32910);

    long_phase();

    /* A section that was never contended: the whole round trip stays in the
     * guest code, including three levels of recursion. */
    for (U i = 0; i < 1000; ++i)
    {
        EnterCriticalSection(quiet + 1);
        EnterCriticalSection(quiet + 1);
        EnterCriticalSection(quiet + 1);
        LeaveCriticalSection(quiet + 1);
        LeaveCriticalSection(quiet + 1);
        LeaveCriticalSection(quiet + 1);
    }
    check(record(quiet + 1)[4] == 1 && record(quiet + 1)[1] == 0, 32911);

    /* A deleted section keeps neither tag nor record; its Leave is ignored
     * by the original entry. */
    DeleteCriticalSection(quiet + 1);
    check(!(quiet[1] & 255) && !quiet[2], 32912);
    LeaveCriticalSection(quiet + 1);
    DeleteCriticalSection(section + 1);

    U *results = (U *)VirtualAlloc((void *)0x30000000, 4096, 0x3000, 4);
    check(results == (U *)0x30000000, 32913);
    results[0] = counter;
    results[1] = collisions;
    results[2] = spins;
    results[3] = long_rounds;
    ExitProcess(0);
}
