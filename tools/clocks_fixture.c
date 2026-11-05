/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The clocks and the waits of the port against the wall clock of the host.
 * The guest measures with its own clocks and publishes one record per phase;
 * tools/test_clocks_guest.cpp takes the host time of every phase boundary and
 * compares. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetTickCount(void);
API U timeGetTime(void);
API U QueryPerformanceCounter(void *);
API U QueryPerformanceFrequency(void *);
API void Sleep(U);
API void *VirtualAlloc(void *, U, U, U);
API void ExitProcess(U);
API U timeSetEvent(U, U, void *, U, U);
API U timeKillEvent(U);
API U timeBeginPeriod(U);
API U timeEndPeriod(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SetTimer(U, U, U, void *);
API U KillTimer(U, U);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API U DestroyWindow(U);

#define RECORDS 0x30000000u
#define FIELDS 6u
/* phase, guest milliseconds, performance-counter ticks, count, extra, ready */
static U *records;
static U phases;

/* The runner takes the host time of every phase boundary. Without a handshake
 * two boundaries can fall into one of its polls, and the wall time of the first
 * then carries the work of the second. The guest therefore waits until the
 * runner has acknowledged the record by writing 2 into its last field. */
static void publish(U phase, U guest_ms, U ticks, U count, U extra)
{
    U *p = records + 1 + phases * FIELDS;
    p[0] = phase; p[1] = guest_ms; p[2] = ticks; p[3] = count; p[4] = extra; p[5] = 1;
    records[0] = ++phases;
    const U start = GetTickCount();
    while (p[5] != 2 && GetTickCount() - start < 2000)
        Sleep(1);
}

static unsigned long long counter(void)
{
    unsigned long long value = 0;
    QueryPerformanceCounter(&value);
    return value;
}

static unsigned long long frequency;
/* The raw difference in counter ticks. A 64-bit division would pull __aulldiv
 * out of the CRT, which this freestanding fixture does not link, so the runner
 * divides by the frequency it reads from phase 1. One second is about 1.2
 * million ticks, so 32 bits carry an hour. */
static U counter_ticks(unsigned long long from, unsigned long long to)
{
    return (U)(to - from);
}

static U timer_hits;
static U timer_window;
static U __stdcall window_procedure(U hwnd, U message, U wp, U lp)
{
    if (message == 0x113) /* WM_TIMER */
        ++timer_hits;
    return DefWindowProcA(hwnd, message, wp, lp);
}

static U media_hits;
static void __stdcall media_callback(U id, U message, U user, U r1, U r2)
{
    (void)id; (void)message; (void)user; (void)r1; (void)r2;
    ++media_hits;
}

/* The window class the timer phases need. Fields of WNDCLASSA. */
static U klass[10];
static char class_name[] = "NEO98CLOCKS";

/* Pumps the message queue and lets the scheduler run until GetTickCount has
 * moved on by `ms`. Every phase that counts callbacks needs this: a guest that
 * never asks for messages never gets a WM_TIMER. */
static void pump(U ms)
{
    U message[8];
    const U start = GetTickCount();
    while (GetTickCount() - start < ms)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        Sleep(1);
    }
}

void run_clocks_test(void)
{
    records = VirtualAlloc((void *)RECORDS, 65536, 0x3000, 4);
    if (records != (U *)RECORDS)
        ExitProcess(7400);
    records[0] = 0;
    QueryPerformanceFrequency(&frequency);
    if (!frequency)
        ExitProcess(7401);
    publish(1, (U)(frequency & 0xffffffffu), (U)(frequency >> 32), 0, 0);

    /* Phase 10: two hundred Sleep(1). Under Win98 each one is at least one
     * millisecond, so the whole phase cannot be shorter than 200 ms. */
    {
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        for (U i = 0; i < 200; ++i) Sleep(1);
        publish(10, GetTickCount() - t0, counter_ticks(c0, counter()), 200, 1);
    }
    /* Phase 11: twenty Sleep(10). */
    {
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        for (U i = 0; i < 20; ++i) Sleep(10);
        publish(11, GetTickCount() - t0, counter_ticks(c0, counter()), 20, 10);
    }
    /* Phase 12: four Sleep(50). */
    {
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        for (U i = 0; i < 4; ++i) Sleep(50);
        publish(12, GetTickCount() - t0, counter_ticks(c0, counter()), 4, 50);
    }
    /* Phase 20: GetTickCount against itself for 500 ms, how many distinct
     * values it took and the largest step. That is its resolution: the VMM
     * cell moves with the timer interrupt, which VMM itself asks for every
     * MinTimeSlice = 16 ms (src/win98_system_clock.h). */
    {
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        U last = t0, steps = 0, biggest = 0;
        while (GetTickCount() - t0 < 500)
        {
            const U now = GetTickCount();
            if (now != last) { ++steps; if (now - last > biggest) biggest = now - last; last = now; }
        }
        publish(20, GetTickCount() - t0, counter_ticks(c0, counter()), steps, biggest);
    }
    /* Phase 22: the same after timeBeginPeriod(1). VTD programs the PIT for
     * one interrupt per millisecond, and the cell follows it. */
    {
        timeBeginPeriod(1);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        U last = t0, steps = 0, biggest = 0;
        while (GetTickCount() - t0 < 500)
        {
            const U now = GetTickCount();
            if (now != last) { ++steps; if (now - last > biggest) biggest = now - last; last = now; }
        }
        timeEndPeriod(1);
        publish(22, GetTickCount() - t0, counter_ticks(c0, counter()), steps, biggest);
    }
    /* Phase 23: GetTickCount while the same loop also asks timeGetTime.
     * Get_System_Time brings the cell up to date before it reads it. */
    {
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        U last = t0, steps = 0, biggest = 0;
        while (GetTickCount() - t0 < 500)
        {
            timeGetTime();
            const U now = GetTickCount();
            if (now != last) { ++steps; if (now - last > biggest) biggest = now - last; last = now; }
        }
        publish(23, GetTickCount() - t0, counter_ticks(c0, counter()), steps, biggest);
    }
    /* Phase 24: the bounds of VTDAPI. A period of 0 is TIMERR_NOCANDO (97),
     * one of 54 or more succeeds without effect, and an end without a
     * begin is TIMERR_NOCANDO. */
    {
        const U zero = timeBeginPeriod(0);
        const U long_begin = timeBeginPeriod(200);
        const U long_end = timeEndPeriod(200);
        const U unmatched = timeEndPeriod(7);
        publish(24, long_begin | long_end << 16, 0, zero, unmatched);
    }
    /* Phase 21: the same for timeGetTime. */
    {
        const U t0 = timeGetTime(); const unsigned long long c0 = counter();
        U last = t0, steps = 0, reads = 0;
        while (timeGetTime() - t0 < 500)
        {
            const U now = timeGetTime();
            ++reads;
            if (now != last) { ++steps; last = now; }
        }
        publish(21, timeGetTime() - t0, counter_ticks(c0, counter()), steps, reads);
    }

    /* The window the WM_TIMER phases need. */
    klass[1] = (U)(void *)window_procedure;
    klass[4] = GetModuleHandleA(0);
    klass[9] = (U)(void *)class_name; /* lpszClassName is at offset 36 */
    if (!RegisterClassA(klass))
        ExitProcess(7402);
    timer_window = CreateWindowExA(0, class_name, class_name, 0x80000000u, 0, 0, 64, 64, 0, 0, klass[4], 0);
    if (!timer_window)
        ExitProcess(7403);

    /* Phase 30: SetTimer with 1 ms over one second. Win98 has a lower bound
     * here, and this phase measures which one this port has. */
    {
        timer_hits = 0;
        const U id = SetTimer(timer_window, 1, 1, 0);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        pump(1000);
        const U hits = timer_hits;
        KillTimer(timer_window, id);
        publish(30, GetTickCount() - t0, counter_ticks(c0, counter()), hits, 1);
    }
    /* Phase 31: SetTimer with 10 ms. */
    {
        timer_hits = 0;
        const U id = SetTimer(timer_window, 2, 10, 0);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        pump(1000);
        const U hits = timer_hits;
        KillTimer(timer_window, id);
        publish(31, GetTickCount() - t0, counter_ticks(c0, counter()), hits, 10);
    }
    /* Phase 32: SetTimer with 55 ms, the tick of the original timer. */
    {
        timer_hits = 0;
        const U id = SetTimer(timer_window, 3, 55, 0);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        pump(1000);
        const U hits = timer_hits;
        KillTimer(timer_window, id);
        publish(32, GetTickCount() - t0, counter_ticks(c0, counter()), hits, 55);
    }
    /* Phase 40: a periodic multimedia timer of 1 ms over one second. */
    {
        media_hits = 0;
        timeBeginPeriod(1);
        const U id = timeSetEvent(1, 1, (void *)media_callback, 0, 1);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        pump(1000);
        const U hits = media_hits;
        timeKillEvent(id);
        timeEndPeriod(1);
        publish(40, GetTickCount() - t0, counter_ticks(c0, counter()), hits, 1);
    }
    /* Phase 41: the same with 10 ms. */
    {
        media_hits = 0;
        const U id = timeSetEvent(10, 1, (void *)media_callback, 0, 1);
        const U t0 = GetTickCount(); const unsigned long long c0 = counter();
        pump(1000);
        const U hits = media_hits;
        timeKillEvent(id);
        publish(41, GetTickCount() - t0, counter_ticks(c0, counter()), hits, 10);
    }
    DestroyWindow(timer_window);
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    run_clocks_test();
}
