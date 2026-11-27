// SPDX-License-Identifier: GPL-2.0-or-later
// The time and date pictures of Regional Settings
// (tools/test_locale_settings_host.py).
//
//   LOCSET.EXE set   SetLocaleInfoA with the time format HH:mm:ss and the
//                    short date dd.MM.yyyy, and what the locale answers then
//   LOCSET.EXE back  the same with tt h:mm:ss and M/d/yy
//   LOCSET.EXE get   what a new session answers
//   LOCSET.EXE separators  a separator set alone rewrites its picture, as
//                    Regional Settings relies on, and a time format names
//                    its separator
//   LOCSET.EXE defaults  a value equal to the locale's own is removed from
//                    HKCU\Control Panel\International instead of stored,
//                    the first day of the week, LOCALE_ITIME
//   LOCSET.EXE typed  what the locale answers for the boxes of Regional
//                    Settings that take typed text, and the dates, times,
//                    numbers and amounts formatted with them
//                    (tools/test_regional_typed_host.py)
//   LOCSET.EXE german  the user's locale German (Germany): Locale of
//                    HKCU\Control Panel\International, InvalidateNLSCache,
//                    and what the locale answers then
// Each writes C:\LOCSET.TXT. The lists of EnumTimeFormatsA and
// EnumDateFormatsA are part of it: the picture the user has set comes first
// and is not repeated among those of the locale.
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
static BOOL CALLBACK listed(LPSTR picture)
{
    used += wsprintfA(text + used, "%s|", picture);
    return TRUE;
}
static int ends(const char *command, const char *word)
{
    int n = lstrlenA(command), m = lstrlenA(word);
    return n > m && !lstrcmpA(command + n - m, word) && command[n - m - 1] == ' ';
}

static void set(const char *name, LCTYPE type, const char *value)
{
    BOOL answer;
    SetLastError(0);
    answer = SetLocaleInfoA(LOCALE_USER_DEFAULT, type, value);
    used += wsprintfA(text + used, "%s=%d %lu\r\n", name, answer, answer ? 0 : GetLastError());
}
static void separators(void)
{
    SYSTEMTIME afternoon = {2001, 2, 6, 3, 15, 4, 5, 0};
    char value[80];
    DWORD written = 0;
    HANDLE file;
    SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, "h:mm:ss tt");
    SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, "M/d/yy");
    field("separator of h:mm:ss tt", LOCALE_STIME);
    set("time separator .", LOCALE_STIME, ".");
    field("time format with .", LOCALE_STIMEFORMAT);
    GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "time with .=%s\r\n", value);
    set("date separator -", LOCALE_SDATE, "-");
    field("short date with -", LOCALE_SSHORTDATE);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "date with -=%s\r\n", value);
    // Only what equals the first separator of the picture is replaced.
    SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_SSHORTDATE, "dd/MM-yyyy");
    set("date separator . of two", LOCALE_SDATE, ".");
    field("short date of two separators", LOCALE_SSHORTDATE);
    // The marker and quoted text keep what leads to them.
    SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, "tt h:mm' Uhr 'ss");
    field("separator of tt h:mm' Uhr 'ss", LOCALE_STIME);
    set("time separator empty", LOCALE_STIME, "");
    field("time format without separator", LOCALE_STIMEFORMAT);
    set("time separator h", LOCALE_STIME, "h");
    set("time separator 1", LOCALE_STIME, "1");
    set("date separator of four", LOCALE_SDATE, "....");
    set("date separator M", LOCALE_SDATE, "M");
    SetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_STIMEFORMAT, "HHmm");
    field("separator of HHmm", LOCALE_STIME);
    file = CreateFileA("C:\\LOCSET.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}

// The stored value of HKCU\Control Panel\International, or "absent".
static void stored(const char *label, const char *name)
{
    char value[80];
    DWORD size = sizeof value, type = 0;
    HKEY key;
    lstrcpyA(value, "absent");
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\International", &key))
    {
        if (RegQueryValueExA(key, name, 0, &type, (BYTE *)value, &size))
            lstrcpyA(value, "absent");
        RegCloseKey(key);
    }
    used += wsprintfA(text + used, "%s=%s\r\n", label, value);
}
static void finish(void)
{
    DWORD written = 0;
    HANDLE file = CreateFileA("C:\\LOCSET.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
static void defaults(void)
{
    SYSTEMTIME afternoon = {2001, 2, 6, 3, 15, 4, 5, 0};
    char value[80];
    set("first day 0", LOCALE_IFIRSTDAYOFWEEK, "0");
    stored("stored first day 0", "iFirstDayOfWeek");
    field("first day after 0", LOCALE_IFIRSTDAYOFWEEK);
    set("first day 6", LOCALE_IFIRSTDAYOFWEEK, "6");
    stored("stored first day 6", "iFirstDayOfWeek");
    field("first day after 6", LOCALE_IFIRSTDAYOFWEEK);
    set("first day 7", LOCALE_IFIRSTDAYOFWEEK, "7");
    set("list ;", LOCALE_SLIST, ";");
    stored("stored list ;", "sList");
    set("list ,", LOCALE_SLIST, ",");
    stored("stored list ,", "sList");
    set("time format HH:mm:ss", LOCALE_STIMEFORMAT, "HH:mm:ss");
    stored("stored HH:mm:ss", "sTimeFormat");
    stored("stored iTime of HH:mm:ss", "iTime");
    set("time format h:mm:ss tt", LOCALE_STIMEFORMAT, "h:mm:ss tt");
    stored("stored h:mm:ss tt", "sTimeFormat");
    stored("stored iTime of h:mm:ss tt", "iTime");
    stored("stored iTLZero of h:mm:ss tt", "iTLZero");
    stored("stored sTime of h:mm:ss tt", "sTime");
    set("iTime 1", LOCALE_ITIME, "1");
    field("time format after iTime 1", LOCALE_STIMEFORMAT);
    stored("stored iTime 1", "iTime");
    GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "time after iTime 1=%s\r\n", value);
    set("iTime 0", LOCALE_ITIME, "0");
    field("time format after iTime 0", LOCALE_STIMEFORMAT);
    stored("stored time format after iTime 0", "sTimeFormat");
    set("short date M/d/yy", LOCALE_SSHORTDATE, "M/d/yy");
    stored("stored M/d/yy", "sShortDate");
    stored("stored iDate of M/d/yy", "iDate");
    stored("stored sDate of M/d/yy", "sDate");
    finish();
}
static void typed(void)
{
    SYSTEMTIME afternoon = {2001, 2, 6, 3, 15, 4, 5, 0};
    char value[80];
    field("short date", LOCALE_SSHORTDATE);
    field("long date", LOCALE_SLONGDATE);
    field("date separator", LOCALE_SDATE);
    field("time format", LOCALE_STIMEFORMAT);
    field("time separator", LOCALE_STIME);
    field("am", LOCALE_S1159);
    field("pm", LOCALE_S2359);
    field("decimal", LOCALE_SDECIMAL);
    field("thousand", LOCALE_STHOUSAND);
    field("list", LOCALE_SLIST);
    field("currency", LOCALE_SCURRENCY);
    field("money decimal", LOCALE_SMONDECIMALSEP);
    field("money thousand", LOCALE_SMONTHOUSANDSEP);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "short=%s\r\n", value);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_LONGDATE, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "long=%s\r\n", value);
    GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &afternoon, 0, value, sizeof value);
    used += wsprintfA(text + used, "time=%s\r\n", value);
    GetNumberFormatA(LOCALE_USER_DEFAULT, 0, "1234567.5", 0, value, sizeof value);
    used += wsprintfA(text + used, "number=%s\r\n", value);
    GetCurrencyFormatA(LOCALE_USER_DEFAULT, 0, "1234567.5", 0, value, sizeof value);
    used += wsprintfA(text + used, "amount=%s\r\n", value);
    finish();
}
static void german(void)
{
    HKEY key;
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\International", &key))
    {
        RegSetValueExA(key, "Locale", 0, REG_SZ, (const BYTE *)"00000407", 9);
        RegCloseKey(key);
    }
    {
        // Not declared by the headers; Regional Settings calls it after a
        // new locale.
        BOOL(WINAPI * invalidate)(void) =
            (BOOL(WINAPI *)(void))GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "InvalidateNLSCache");
        if (invalidate)
            invalidate();
    }
    used += wsprintfA(text + used, "user locale=%lx\r\n", (unsigned long)GetUserDefaultLCID());
    field("first day", LOCALE_IFIRSTDAYOFWEEK);
    field("short date", LOCALE_SSHORTDATE);
    field("time format", LOCALE_STIMEFORMAT);
    field("decimal", LOCALE_SDECIMAL);
    finish();
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    SYSTEMTIME afternoon = {2001, 2, 6, 3, 15, 4, 5, 0}; // Saturday, 3 February 2001, 15:04:05
    char value[80];
    DWORD written = 0;
    HANDLE file;
    if (ends(command, "separators")) separators();
    if (ends(command, "defaults")) defaults();
    if (ends(command, "german")) german();
    if (ends(command, "typed")) typed();
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
    used += wsprintfA(text + used, "time formats=");
    EnumTimeFormatsA(listed, LOCALE_USER_DEFAULT, 0);
    used += wsprintfA(text + used, "\r\nshort dates=");
    EnumDateFormatsA(listed, LOCALE_USER_DEFAULT, DATE_SHORTDATE);
    used += wsprintfA(text + used, "\r\nlong dates=");
    EnumDateFormatsA(listed, LOCALE_USER_DEFAULT, DATE_LONGDATE);
    used += wsprintfA(text + used, "\r\nother locale=");
    EnumTimeFormatsA(listed, MAKELCID(MAKELANGID(LANG_GERMAN, SUBLANG_GERMAN), SORT_DEFAULT), 0);
    used += wsprintfA(text + used, "\r\n");
    file = CreateFileA("C:\\LOCSET.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
