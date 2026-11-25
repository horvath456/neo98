/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The clocks of Windows count from its start. Writes C:\CLOCK32.TXT, seven
   ints: GetTickCount, timeGetTime, QueryPerformanceCounter in milliseconds
   (the counter over its frequency), the time of a message this thread
   posted itself, GetMessageTime after it, GetTickCount after Sleep(500),
   and the frequency of the counter. tools/test_clock_start_host.py */
#include <windows.h>
#include <mmsystem.h>

void mainCRTStartup(void)
{
    int results[7] = {0};
    LARGE_INTEGER counter, frequency;
    MSG message;
    results[0] = (int)GetTickCount();
    results[1] = (int)timeGetTime();
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    results[2] = (int)(counter.QuadPart * 1000 / frequency.QuadPart);
    PostThreadMessageA(GetCurrentThreadId(), WM_APP, 0, 0);
    GetMessageA(&message, NULL, 0, 0);
    results[3] = (int)message.time;
    results[4] = (int)GetMessageTime();
    Sleep(500);
    results[5] = (int)GetTickCount();
    results[6] = (int)frequency.QuadPart;
    HANDLE file = CreateFileA("C:\\CLOCK32.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;
    WriteFile(file, results, sizeof results, &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
