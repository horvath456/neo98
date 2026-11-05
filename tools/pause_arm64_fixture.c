/* SPDX-License-Identifier: GPL-2.0-or-later
   The ARM64 guest of tools/test_pause_arm64_host.py. On a
   timer of 50 ms it reads GetTickCount, QueryPerformanceCounter, the wall
   clock and the counter of its processor (CNTVCT_EL0, which no call of the
   system stands behind) and keeps the largest step of each. After six
   seconds of its tick count it writes them to C:\APAUSE.TXT, in
   milliseconds. */
#include <windows.h>

static unsigned long long Counter(void)
{
    unsigned long long value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}
static unsigned long long CounterFrequency(void)
{
    unsigned long long value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}
static unsigned long long Wall(void)
{
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    return ((unsigned long long)time.dwHighDateTime << 32 | time.dwLowDateTime) / 10000;
}
static unsigned long long Performance(void)
{
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return (unsigned long long)value.QuadPart * 1000 / (unsigned long long)frequency.QuadPart;
}

static DWORD tick0, tick_last, tick_step, timers;
static unsigned long long qpc0, qpc_last, qpc_step, cnt0, cnt_last, cnt_step, wall0, wall_last, wall_step, frequency;
static int done;

static void Finish(HWND window)
{
    char text[400];
    DWORD written;
    const int n = wsprintfA(text,
        "tick_elapsed=%u\r\nqpc_elapsed=%u\r\ncounter_elapsed=%u\r\nwall_elapsed=%u\r\n"
        "tick_step=%u\r\nqpc_step=%u\r\ncounter_step=%u\r\nwall_step=%u\r\ntimers=%u\r\nfrequency=%u\r\n",
        (unsigned)(GetTickCount() - tick0), (unsigned)(Performance() - qpc0),
        (unsigned)((Counter() - cnt0) * 1000 / frequency), (unsigned)(Wall() - wall0),
        (unsigned)tick_step, (unsigned)qpc_step, (unsigned)(cnt_step * 1000 / frequency), (unsigned)wall_step,
        (unsigned)timers, (unsigned)frequency);
    HANDLE file = CreateFileA("C:\\APAUSE.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(file, text, (DWORD)n, &written, NULL);
    CloseHandle(file);
    SetWindowTextA(window, "APause done");
}
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_TIMER && !done)
    {
        const DWORD tick = GetTickCount();
        const unsigned long long qpc = Performance(), cnt = Counter(), wall = Wall();
        ++timers;
        if (tick - tick_last > tick_step) tick_step = tick - tick_last;
        if (qpc - qpc_last > qpc_step) qpc_step = qpc - qpc_last;
        if (cnt - cnt_last > cnt_step) cnt_step = cnt - cnt_last;
        if (wall - wall_last > wall_step) wall_step = wall - wall_last;
        tick_last = tick; qpc_last = qpc; cnt_last = cnt; wall_last = wall;
        if (tick - tick0 >= 6000)
        {
            done = 1;
            KillTimer(window, 1);
            Finish(window);
        }
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, wParam, lParam);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR line, int show)
{
    WNDCLASSA c = {0};
    MSG message;
    HWND window;
    (void)previous; (void)line; (void)show;
    c.lpfnWndProc = Procedure;
    c.hInstance = instance;
    c.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    c.lpszClassName = "PauseArm64";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "PauseArm64", "APause", WS_OVERLAPPEDWINDOW, 40, 40, 300, 200, NULL, NULL, instance, NULL);
    ShowWindow(window, SW_SHOW);
    frequency = CounterFrequency();
    if (!frequency) frequency = 24000000;
    tick0 = tick_last = GetTickCount();
    qpc0 = qpc_last = Performance();
    cnt0 = cnt_last = Counter();
    wall0 = wall_last = Wall();
    SetTimer(window, 1, 50, NULL);
    SetWindowTextA(window, "APause ready");
    while (GetMessageA(&message, NULL, 0, 0) > 0) DispatchMessageA(&message);
    return 0;
}
