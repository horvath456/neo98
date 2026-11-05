/* SPDX-License-Identifier: GPL-2.0-or-later
   The guest of tools/test_pause_host.py : it reads every
   counter (RDTSC and, where CPUID offers it, RDTSCP among them) and the wall
   clock on a timer of 50 ms, keeps the largest step of
   each between two timer messages, sleeps and waits with a timeout across the
   pause, plays a tone, counts the keys and presses it gets, and writes all of
   it to D:\PAUSE.TXT after eight seconds of its own tick count. */
typedef unsigned U;
typedef struct { U lo, hi; } Q;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
typedef struct { unsigned short tag, channels; U rate, bytes_per_second; unsigned short align, bits, extra; } WAVEFORMATEX;
typedef struct { char *data; U length, recorded, user, flags, loops; void *next; U reserved; } WAVEHDR;
typedef struct { U type; U value; U more[2]; } MMTIME;
typedef struct { unsigned short year, month, weekday, day, hour, minute, second, ms; } SYSTEMTIME;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, ShowWindow, U, int);
I(int, SetWindowTextA, U, const char *);
I(U, SetTimer, U, U, U, void *);
I(int, KillTimer, U, U);
I(U, SetFocus, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(U, GetTickCount, void);
I(int, QueryPerformanceCounter, Q *);
I(int, QueryPerformanceFrequency, Q *);
I(void, GetSystemTimeAsFileTime, Q *);
I(void, GetLocalTime, SYSTEMTIME *);
I(U, CreateThread, void *, U, void *, void *, U, U *);
I(void, Sleep, U);
I(U, CreateEventA, void *, int, int, const char *);
I(U, WaitForSingleObject, U, U);
I(U, CreateFileA, const char *, U, U, void *, U, U, U);
I(int, WriteFile, U, const void *, U, U *, void *);
I(int, CloseHandle, U);
I(void, ExitProcess, U);
I(U, timeGetTime, void);
I(U, waveOutOpen, U *, U, const WAVEFORMATEX *, U, U, U);
I(U, waveOutPrepareHeader, U, WAVEHDR *, U);
I(U, waveOutWrite, U, WAVEHDR *, U);
I(U, waveOutGetPosition, U, MMTIME *, U);

static Q Sub(Q a, Q b) { Q r; r.lo = a.lo - b.lo; r.hi = a.hi - b.hi - (a.lo < b.lo); return r; }
static int Less(Q a, Q b) { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); }

/* The time stamp counter of the processor, which no call into the system
   answers. RDTSCP only where CPUID 80000001h EDX bit 27 offers it. */
static Q Rdtsc(void)
{
    Q r;
    __asm__ volatile("rdtsc" : "=a"(r.lo), "=d"(r.hi));
    return r;
}
static Q Rdtscp(void)
{
    Q r;
    U aux;
    __asm__ volatile(".byte 0x0f, 0x01, 0xf9" : "=a"(r.lo), "=d"(r.hi), "=c"(aux));
    (void)aux;
    return r;
}
static int HasRdtscp(void)
{
    U a = 0x80000000u, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (a < 0x80000001u) return 0;
    a = 0x80000001u;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (d >> 27) & 1;
}

static U window, wave, timers, backwards, key_downs, key_ups, key_c, presses, last_key;
static U tick0, tick_last, tick_step, mm0, mm_last, mm_step, wave_last, wave_step;
static Q qpc0, qpc_last, qpc_step, wall0, wall_last, wall_step, frequency;
/* tsc_rate, qpc_rate: both counters over the first half second, which gives
   the test the rate of RDTSC in terms of QueryPerformanceCounter. */
static Q tsc0, tsc_last, tsc_step, tsc_rate, qpc_rate, tscp0, tscp_last, tscp_step;
static U rdtscp;
static U sleep_tick, wait_tick, wait_result;
static Q sleep_wall, wait_wall;
static char tone[22050 * 14];
static WAVEHDR header;
static int done;

static U __stdcall Sleeper(void *unused)
{
    Q before, after;
    const U start = GetTickCount();
    (void)unused;
    GetSystemTimeAsFileTime(&before);
    Sleep(4000);
    sleep_tick = GetTickCount() - start;
    GetSystemTimeAsFileTime(&after);
    sleep_wall = Sub(after, before);
    return 0;
}
static U __stdcall Waiter(void *unused)
{
    Q before, after;
    const U event = CreateEventA(0, 1, 0, 0), start = GetTickCount();
    (void)unused;
    GetSystemTimeAsFileTime(&before);
    wait_result = WaitForSingleObject(event, 4000);
    wait_tick = GetTickCount() - start;
    GetSystemTimeAsFileTime(&after);
    wait_wall = Sub(after, before);
    return 0;
}
static U WavePosition(void)
{
    MMTIME time = {0};
    time.type = 2;   /* TIME_SAMPLES */
    if (!wave || waveOutGetPosition(wave, &time, sizeof time)) return 0;
    return time.value;
}
static void Finish(void)
{
    static char text[1600];
    SYSTEMTIME local;
    Q qpc, wall;
    U file, written, n;
    const U tick = GetTickCount(), mm = timeGetTime();
    QueryPerformanceCounter(&qpc);
    GetSystemTimeAsFileTime(&wall);
    GetLocalTime(&local);
    Q tsc = Sub(Rdtsc(), tsc0), tscp = {0, 0};
    if (rdtscp) tscp = Sub(Rdtscp(), tscp0);
    qpc = Sub(qpc, qpc0);
    wall = Sub(wall, wall0);
    n = wsprintfA(text, "tsc_elapsed=%u,%u\r\ntsc_step=%u,%u\r\ntsc_rate=%u,%u\r\nqpc_rate=%u,%u\r\n"
                        "rdtscp=%u\r\ntscp_elapsed=%u,%u\r\ntscp_step=%u,%u\r\n",
                  tsc.hi, tsc.lo, tsc_step.hi, tsc_step.lo, tsc_rate.hi, tsc_rate.lo, qpc_rate.hi, qpc_rate.lo,
                  rdtscp, tscp.hi, tscp.lo, tscp_step.hi, tscp_step.lo);
    n += wsprintfA(text + n,
        "tick_elapsed=%u\r\nmm_elapsed=%u\r\nqpc_elapsed=%u,%u\r\nwall_elapsed=%u,%u\r\nfrequency=%u,%u\r\n"
        "tick_step=%u\r\nmm_step=%u\r\nqpc_step=%u,%u\r\nwall_step=%u,%u\r\nwave_step=%u\r\nwave_position=%u\r\n"
        "timers=%u\r\nbackwards=%u\r\n",
        tick - tick0, mm - mm0, qpc.hi, qpc.lo, wall.hi, wall.lo, frequency.hi, frequency.lo,
        tick_step, mm_step, qpc_step.hi, qpc_step.lo, wall_step.hi, wall_step.lo, wave_step, WavePosition(),
        timers, backwards);
    n += wsprintfA(text + n,
        "sleep_tick=%u\r\nsleep_wall=%u,%u\r\nwait_tick=%u\r\nwait_wall=%u,%u\r\nwait_result=%u\r\n"
        "key_downs=%u\r\nkey_ups=%u\r\nkey_c=%u\r\nlast_key=%u\r\npresses=%u\r\nlocal=%u:%u:%u\r\n",
        sleep_tick, sleep_wall.hi, sleep_wall.lo, wait_tick, wait_wall.hi, wait_wall.lo, wait_result,
        key_downs, key_ups, key_c, last_key, presses, (U)local.hour, (U)local.minute, (U)local.second);
    file = CreateFileA("D:\\PAUSE.TXT", 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, n, &written, 0);
    CloseHandle(file);
    SetWindowTextA(window, "Pause done");
}
static void Sample(void)
{
    Q qpc, wall, step, tsc = Rdtsc(), tscp = {0, 0};
    U i, tick = GetTickCount(), mm = timeGetTime(), position = WavePosition();
    QueryPerformanceCounter(&qpc);
    GetSystemTimeAsFileTime(&wall);
    if (rdtscp) tscp = Rdtscp();
    /* Many reads in a row: the way through the fast gates. None may go back. */
    for (i = 0; i < 300; ++i)
    {
        Q next;
        const Q stamp = Rdtsc();
        const U t = GetTickCount(), m = timeGetTime();
        QueryPerformanceCounter(&next);
        if ((int)(t - tick) < 0 || (int)(m - mm) < 0 || Less(next, qpc) || Less(stamp, tsc)) ++backwards;
        tick = t; mm = m; qpc = next; tsc = stamp;
        if (rdtscp)
        {
            const Q second = Rdtscp();
            if (Less(second, tscp)) ++backwards;
            tscp = second;
        }
    }
    step = Sub(tsc, tsc_last);
    if (Less(tsc_step, step)) tsc_step = step;
    step = Sub(tscp, tscp_last);
    if (Less(tscp_step, step)) tscp_step = step;
    tsc_last = tsc; tscp_last = tscp;
    if (!tsc_rate.lo && !tsc_rate.hi && tick - tick0 >= 500)
    {
        tsc_rate = Sub(tsc, tsc0);
        qpc_rate = Sub(qpc, qpc0);
    }
    if (tick - tick_last > tick_step) tick_step = tick - tick_last;
    if (mm - mm_last > mm_step) mm_step = mm - mm_last;
    if (position - wave_last > wave_step) wave_step = position - wave_last;
    step = Sub(qpc, qpc_last);
    if (Less(qpc_step, step)) qpc_step = step;
    step = Sub(wall, wall_last);
    if (Less(wall_step, step)) wall_step = step;
    tick_last = tick; mm_last = mm; qpc_last = qpc; wall_last = wall; wave_last = position;
}
static U __stdcall Procedure(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x113 && !done)   /* WM_TIMER */
    {
        ++timers;
        Sample();
        if (GetTickCount() - tick0 >= 8000)
        {
            done = 1;
            KillTimer(hwnd, 1);
            Finish();
        }
        return 0;
    }
    if (message == 0x100) { ++key_downs; last_key = wparam; if (wparam == 'C') ++key_c; return 0; }
    if (message == 0x101) { ++key_ups; return 0; }
    if (message == 0x201) { ++presses; return 0; }
    if (message == 2) ExitProcess(0);
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    static const WAVEFORMATEX format = {1, 1, 22050, 22050, 1, 8, 0};
    WNDCLASSA c = {0};
    MSG message;
    U i, id;
    c.proc = Procedure;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "PauseFixture";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "PauseFixture", "Pause", 0x00cf0000, 40, 40, 400, 300, 0, 0, c.instance, 0);
    ShowWindow(window, 1);
    SetFocus(window);
    /* 441 Hz, a square of 50 samples, for fourteen seconds. */
    for (i = 0; i < sizeof tone; ++i) tone[i] = (char)(i % 50 < 25 ? 0xb0 : 0x50);
    if (!waveOutOpen(&wave, 0xffffffff, &format, 0, 0, 0))
    {
        header.data = tone;
        header.length = sizeof tone;
        waveOutPrepareHeader(wave, &header, sizeof header);
        waveOutWrite(wave, &header, sizeof header);
    }
    QueryPerformanceFrequency(&frequency);
    tick0 = tick_last = GetTickCount();
    mm0 = mm_last = timeGetTime();
    QueryPerformanceCounter(&qpc0); qpc_last = qpc0;
    rdtscp = HasRdtscp();
    tsc0 = tsc_last = Rdtsc();
    if (rdtscp) tscp0 = tscp_last = Rdtscp();
    GetSystemTimeAsFileTime(&wall0); wall_last = wall0;
    CreateThread(0, 0, Sleeper, 0, 0, &id);
    CreateThread(0, 0, Waiter, 0, 0, &id);
    SetTimer(window, 1, 50, 0);
    SetWindowTextA(window, "Pause ready");
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(0);
}
