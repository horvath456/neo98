// SPDX-License-Identifier: GPL-2.0-or-later
// The time and date pictures of Regional Settings
// (tools/test_locale_settings_host.py).
//
//   LOCSET.EXE set   SetLocaleInfoA with the time format HH:mm:ss and the
//                    short date dd.MM.yyyy, and what the locale answers then
//   LOCSET.EXE back  the same with tt h:mm:ss and M/d/yy
//   LOCSET.EXE get   what a new session answers
// Each writes C:\LOCSET.TXT.
#include <windows.h>

static char text[2048];
static int used;

static void field(const char *name, LCTYPE type)
{
    char value[80];
    lstrcpyA(value, "?");
    GetLocaleInfoA(LOCALE_USER_DEFAULT, type, value, sizeof value);
    used += wsprintfA(text + used, "%s=%s\r\n", name, value);
}
static int ends(const char *command, const char *word)
{
    int n = lstrlenA(command), m = lstrlenA(word);
    return n > m && !lstrcmpA(command + n - m, word) && command[n - m - 1] == ' ';
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    SYSTEMTIME afternoon = {2001, 2, 6, 3, 15, 4, 5, 0}; // Saturday, 3 February 2001, 15:04:05
    char value[80];
    DWORD written = 0;
    HANDLE file;
    if (ends(command, "set") || ends(command, "back"))
    {
        const int back = ends(command, "back");
        used += wsprintfA(text + used, "set time=%d\r\n",
                          SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, back ? "tt h:mm:ss" : "HH:mm:ss"));
        used += wsprintfA(text + used, "set date=%d\r\n",
                          SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, back ? "M/d/yy" : "dd.MM.yyyy"));
        SetLastError(0);
        used += wsprintfA(text + used, "set order=%d\r\n", SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_IDATE, "2"));
        used += wsprintfA(text + used, "set order error=%lu\r\n", GetLastError());
    }
    field("time format", LOCALE_STIMEFORMAT);
    field("24 hours", LOCALE_ITIME);
    field("hour zero", LOCALE_ITLZERO);
    field("marker first", LOCALE_ITIMEMARKPOSN);
    field("short date", LOCALE_SSHORTDATE);
    field("date order", LOCALE_IDATE);
    field("date separator", LOCALE_SDATE);
    GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "time=%s\r\n", value);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "date=%s\r\n", value);
    file = CreateFileA("C:\\LOCSET.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
