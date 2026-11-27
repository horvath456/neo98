// SPDX-License-Identifier: GPL-2.0-or-later
// The date and time a program sets and reads (tools/test_guest_clock_host.py).
//
//   GCLOCK.EXE set     SetLocalTime to 3 February 2001, 04:05:06
//   GCLOCK.EXE system  SetSystemTime to 9 August 2007, 10:11:12 UTC
//   GCLOCK.EXE zone    SetTimeZoneInformation to a bias of 300 minutes
//   GCLOCK.EXE get     nothing set
// Each writes the local time, the system time, the bias and the tick count
// afterwards to C:\GCLOCK.TXT.
#include <windows.h>

static char text[1024];
static int used;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static int has(const char *command, const char *word)
{
    int i, n = lstrlenA(word);
    for (; *command; ++command)
    {
        for (i = 0; i < n && command[i] == word[i]; ++i)
            ;
        if (i == n && command[-1] == ' ' && (command[n] == 0 || command[n] == ' '))
            return 1;
    }
    return 0;
}
static void stamp(const char *name, const SYSTEMTIME *t)
{
    used += wsprintfA(text + used, "%s=%04d%02d%02d%02d%02d%02d\r\n", name, t->wYear, t->wMonth, t->wDay, t->wHour,
                      t->wMinute, t->wSecond);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    SYSTEMTIME t;
    TIME_ZONE_INFORMATION zone;
    DWORD written = 0;
    HANDLE file;
    if (has(command, "set"))
    {
        GetLocalTime(&t);
        t.wYear = 2001, t.wMonth = 2, t.wDay = 3, t.wHour = 4, t.wMinute = 5, t.wSecond = 6, t.wMilliseconds = 0;
        line("set", SetLocalTime(&t));
    }
    if (has(command, "system"))
    {
        GetSystemTime(&t);
        t.wYear = 2007, t.wMonth = 8, t.wDay = 9, t.wHour = 10, t.wMinute = 11, t.wSecond = 12, t.wMilliseconds = 0;
        line("set", SetSystemTime(&t));
    }
    if (has(command, "zone"))
    {
        GetTimeZoneInformation(&zone);
        zone.Bias = 300;
        zone.StandardBias = 0;
        zone.DaylightBias = 0;
        zone.StandardDate.wMonth = 0;
        zone.DaylightDate.wMonth = 0;
        line("set", SetTimeZoneInformation(&zone));
    }
    GetLocalTime(&t);
    stamp("local", &t);
    GetSystemTime(&t);
    stamp("system", &t);
    line("zone", (long)GetTimeZoneInformation(&zone));
    line("bias", zone.Bias);
    file = CreateFileA("C:\\GCLOCK.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
