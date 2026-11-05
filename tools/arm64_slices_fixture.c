/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Time slices of ARM64 threads: a thread that computes without calling the
   system does not hold up the other threads, and a procedure the system calls
   while a thread stands interrupted leaves its registers alone.
   tools/test_arm64_stage2_native.py, docs/ARM64-APPS.md. */
#include <windows.h>
#include <stdio.h>

static int checks, failures;
static void Check(int id, int condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    printf("FAIL %d\n", id);
}

/* A computation that keeps integer and floating-point registers busy and
   calls nothing. */
__attribute__((noinline)) static double Compute(unsigned rounds, unsigned long long *integer)
{
    double a = 1.0, b = 0.5, c = 0.25, d = 0.125;
    unsigned long long x = 0x9e3779b97f4a7c15ull, y = 1, z = 2;
    for (unsigned i = 0; i < rounds; ++i)
    {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        y += x >> 11;
        z = z * 6364136223846793005ull + y;
        a = a * 1.0000001 + (double)(x & 0xff) * 1e-9;
        b = b * 0.9999999 + a * 1e-7;
        c += b * 1e-3 - d;
        d = d * 0.5 + c * 1e-6;
    }
    *integer = x ^ y ^ z;
    return a + b + c + d;
}

static volatile LONG worker_done, main_turns;
static DWORD WINAPI Worker(LPVOID parameter)
{
    unsigned long long integer;
    *(double *)parameter = Compute(60000000u, &integer);
    worker_done = 1;
    return 0;
}
static volatile LONG timer_calls;
static void CALLBACK TimeProc(UINT id, UINT message, DWORD_PTR user, DWORD_PTR first, DWORD_PTR second)
{
    /* Uses registers of its own. */
    volatile double scratch = 3.0;
    (void)id; (void)message; (void)user; (void)first; (void)second;
    for (int i = 0; i < 100; ++i) scratch = scratch * 1.5 + i;
    ++timer_calls;
}

int main(void)
{
    double quiet, busy = 0, threaded = 0;
    unsigned long long quiet_integer, busy_integer;
    DWORD start, elapsed, id;
    HANDLE thread;
    UINT timer;

    /* The reference, short enough to end inside one slice or two. */
    quiet = Compute(20000000u, &quiet_integer);

    /* While a second thread computes, this one keeps running. */
    start = GetTickCount();
    thread = CreateThread(NULL, 0, Worker, &threaded, 0, &id);
    Check(1, thread != NULL);
    while (!worker_done && GetTickCount() - start < 20000)
    {
        ++main_turns;
        Sleep(1);
    }
    elapsed = GetTickCount() - start;
    Check(2, worker_done == 1);
    Check(3, main_turns >= 10);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    printf("worker ran %lu ms, main thread had %ld turns meanwhile\n", (unsigned long)elapsed, (long)main_turns);

    /* A multimedia timer calls into this thread while it computes. */
    timer = timeSetEvent(5, 1, TimeProc, 0, TIME_PERIODIC);
    Check(4, timer != 0);
    start = GetTickCount();
    busy = Compute(20000000u, &busy_integer);
    elapsed = GetTickCount() - start;
    timeKillEvent(timer);
    printf("computed %lu ms with %ld timer calls\n", (unsigned long)elapsed, (long)timer_calls);
    Check(5, busy == quiet && busy_integer == quiet_integer);
    Check(6, threaded != 0);
    /* The timer thread got its turns while this one computed. */
    Check(7, timer_calls >= 2);
    printf("SLICES checks=%d failures=%d\n", checks, failures);
    return failures;
}
