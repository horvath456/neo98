/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * The first page of Regional Settings and what follows from it
 * (tools/test_regional_locale_host.py). REGLOC.EXE list writes what
 * EnumSystemLocalesA hands out beside what the registry holds;
 * REGLOC.EXE choose stores a short date for the user's locale, opens
 * INTL.CPL, selects German (Austrian), 0C07, in the list of the first page and
 * presses OK, answers the question about the restart with No, and writes
 * what is stored and what the locale functions answer; REGLOC.EXE after
 * writes the answers again, in a new session. Everything goes into
 * C:\REGLOC.TXT as name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE;
static void put(const char *name, const char *value)
{
    char line[1200];
    DWORD n;
    wsprintfA(line, "%s=%s\r\n", name, value);
    WriteFile(out, line, lstrlenA(line), &n, 0);
}
static void number(const char *name, DWORD value)
{
    char text[16];
    wsprintfA(text, "%lu", value);
    put(name, text);
}
static void hex(const char *name, const char *value)
{
    char text[600];
    int i;
    for (i = 0; value[i] && i < 290; i++)
        wsprintfA(text + 2 * i, "%02x", (unsigned char)value[i]);
    text[2 * i] = 0;
    put(name, text);
}

/* list */
static char listing[4096];
static int calls, stop_after;
static BOOL CALLBACK each(LPSTR name)
{
    if (lstrlenA(listing) + lstrlenA(name) + 2 < (int)sizeof listing) {
        lstrcatA(listing, name);
        lstrcatA(listing, " ");
    }
    return ++calls != stop_after;
}
static void registry(const char *path, BOOL installed, char *into, int capacity)
{
    HKEY key;
    char name[300], data[300];
    DWORD i, n, d, type;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return;
    for (i = 0;; i++) {
        int j, digits;
        n = sizeof name;
        d = sizeof data;
        data[0] = 0;
        if (RegEnumValueA(key, i, name, &n, 0, &type, (BYTE *)data, &d))
            break;
        for (j = 0, digits = 1; name[j]; j++) {
            char c = name[j] | 32;
            digits = digits && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        if (j != 8 || !digits || (installed && !data[0]))
            continue;
        if (lstrlenA(into) + 10 < capacity) {
            lstrcatA(into, name);
            lstrcatA(into, " ");
        }
    }
    RegCloseKey(key);
}
static void enumerate(const char *name, DWORD flags, int stop)
{
    char key[64];
    listing[0] = 0;
    calls = 0;
    stop_after = stop;
    SetLastError(0);
    wsprintfA(key, "%s.result", name);
    number(key, EnumSystemLocalesA(each, flags));
    put(name, listing);
}
static void list(void)
{
    static char expected[4096];
    char value[300];
    HKEY key;
    expected[0] = 0;
    registry("System\\CurrentControlSet\\Control\\Nls\\Locale", TRUE, expected, sizeof expected);
    registry("System\\CurrentControlSet\\Control\\Nls\\AddLocales", TRUE, expected, sizeof expected);
    put("registry installed", expected);
    enumerate("installed", LCID_INSTALLED, 0);
    expected[0] = 0;
    registry("System\\CurrentControlSet\\Control\\Nls\\Locale", FALSE, expected, sizeof expected);
    registry("System\\CurrentControlSet\\Control\\Nls\\AddLocales", FALSE, expected, sizeof expected);
    put("registry supported", expected);
    enumerate("supported", LCID_SUPPORTED, 0);
    /* FALSE from the callback ends the values of one key, not those of the
       next: a value in AddLocales is handed out after it all the same. */
    if (!RegCreateKeyA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Control\\Nls\\AddLocales", &key)) {
        RegSetValueExA(key, "0000ABCD", 0, REG_SZ, (const BYTE *)"Test", 5);
        enumerate("stopped", LCID_SUPPORTED, 1);
        RegDeleteValueA(key, "0000ABCD");
        RegCloseKey(key);
    }
    SetLastError(0);
    number("flags 3", EnumSystemLocalesA(each, 3));
    number("flags 3.error", GetLastError());
    SetLastError(0);
    number("no callback", EnumSystemLocalesA(0, LCID_INSTALLED));
    number("no callback.error", GetLastError());
    /* Texts of a locale come in its own code page, when Windows has its table. */
    value[0] = 0;
    number("russian monday.size", GetLocaleInfoA(0x419, LOCALE_SDAYNAME1, value, sizeof value));
    hex("russian monday", value);
    value[0] = 0;
    number("greek currency.size", GetLocaleInfoA(0x408, LOCALE_SCURRENCY, value, sizeof value));
    hex("greek currency", value);
}

/* choose */
static char box_text[1200];
static HWND sheet;
static BOOL CALLBACK texts(HWND child, LPARAM unused)
{
    char klass[32], text[400];
    GetClassNameA(child, klass, sizeof klass);
    GetWindowTextA(child, text, sizeof text);
    if (!lstrcmpiA(klass, "Static") && text[0] && lstrlenA(box_text) + lstrlenA(text) + 2 < (int)sizeof box_text) {
        lstrcatA(box_text, text);
        lstrcatA(box_text, " ");
    }
    return TRUE;
}
static BOOL CALLBACK windows(HWND window, LPARAM unused)
{
    char klass[32], title[200];
    if (!IsWindowVisible(window))
        return TRUE;
    GetClassNameA(window, klass, sizeof klass);
    GetWindowTextA(window, title, sizeof title);
    if (lstrcmpA(klass, "#32770"))
        return TRUE;
    if (!lstrcmpA(title, "Regional Settings Properties"))
        sheet = window;
    else {
        static HWND seen[16];
        static int count;
        int i;
        for (i = 0; i < count; i++)
            if (seen[i] == window)
                return TRUE;
        if (count < 16)
            seen[count++] = window;
        box_text[0] = 0;
        EnumChildWindows(window, texts, 0);
        for (i = 0; box_text[i]; i++)
            if (box_text[i] == '\r' || box_text[i] == '\n')
                box_text[i] = ' ';
        put("box", box_text);
        PostMessageA(window, WM_COMMAND, IDNO, 0);
    }
    return TRUE;
}
/* What is on the screen when the sheet stays: class and title of each
   visible window. */
static char shown[1200];
static BOOL CALLBACK visible(HWND window, LPARAM unused)
{
    char klass[32], title[100];
    if (!IsWindowVisible(window) || lstrlenA(shown) > 1000)
        return TRUE;
    GetClassNameA(window, klass, sizeof klass);
    GetWindowTextA(window, title, sizeof title);
    lstrcatA(shown, klass);
    lstrcatA(shown, ":");
    lstrcatA(shown, title);
    lstrcatA(shown, "; ");
    return TRUE;
}
static HWND locale_list;
static char names[128][64];
static DWORD names_lcid[128];
static int names_count;
static BOOL CALLBACK name_of(LPSTR text)
{
    DWORD id = 0;
    while (*text) {
        char c = *text++ | 32;
        id = id * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
    }
    if (names_count < 128) {
        names[names_count][0] = 0;
        GetLocaleInfoA(id, LOCALE_SLANGUAGE, names[names_count], sizeof names[0]);
        names_lcid[names_count++] = id;
    }
    return TRUE;
}
static BOOL CALLBACK combo(HWND child, LPARAM unused)
{
    char klass[32];
    GetClassNameA(child, klass, sizeof klass);
    if (!locale_list && !lstrcmpiA(klass, "ComboBox") && IsWindowVisible(child))
        locale_list = child;
    return TRUE;
}
static void international(void)
{
    static const char *names[] = { "Locale", "sShortDate", "sLongDate", "iFirstDayOfWeek", "sCurrency", "sDecimal",
        "sThousand", "sTimeFormat", "iCountry", "sCountry", "sLanguage", "iDate", "sDate", "iCurrency", "iNegCurr" };
    HKEY key;
    static char all[1200];
    char name[64], data[200];
    DWORD i, n, d, type;
    all[0] = 0;
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Control Panel\\International", &key)) {
        for (i = 0;; i++) {
            n = sizeof name;
            d = sizeof data;
            if (RegEnumValueA(key, i, name, &n, 0, &type, (BYTE *)data, &d))
                break;
            if (lstrlenA(all) + n + 2 < sizeof all) {
                lstrcatA(all, name);
                lstrcatA(all, " ");
            }
        }
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            char label[80];
            d = sizeof data;
            data[0] = 0;
            wsprintfA(label, "stored %s", names[i]);
            if (!RegQueryValueExA(key, names[i], 0, &type, (BYTE *)data, &d))
                put(label, data);
        }
        RegCloseKey(key);
    }
    put("stored names", all);
}
static void answers(void)
{
    static const struct { const char *name; DWORD type; } fields[] = {
        { "short date", LOCALE_SSHORTDATE }, { "long date", LOCALE_SLONGDATE }, { "first day", LOCALE_IFIRSTDAYOFWEEK },
        { "currency", LOCALE_SCURRENCY }, { "decimal", LOCALE_SDECIMAL }, { "thousand", LOCALE_STHOUSAND },
        { "time format", LOCALE_STIMEFORMAT }, { "language", LOCALE_SLANGUAGE }, { "date separator", LOCALE_SDATE },
    };
    SYSTEMTIME day = { 2026, 11, 6, 28, 14, 5, 9, 0 };
    char value[200], label[80];
    int i;
    wsprintfA(value, "%04lx", GetUserDefaultLCID());
    put("user locale", value);
    for (i = 0; i < (int)(sizeof fields / sizeof fields[0]); i++) {
        value[0] = 0;
        GetLocaleInfoA(LOCALE_USER_DEFAULT, fields[i].type, value, sizeof value);
        wsprintfA(label, "answer %s", fields[i].name);
        put(label, value);
    }
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &day, 0, value, sizeof value);
    put("formatted short date", value);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_LONGDATE, &day, 0, value, sizeof value);
    put("formatted long date", value);
    GetNumberFormatA(LOCALE_USER_DEFAULT, 0, "-1234567.891", 0, value, sizeof value);
    put("formatted number", value);
    GetCurrencyFormatA(LOCALE_USER_DEFAULT, 0, "-1234567.891", 0, value, sizeof value);
    hex("formatted currency", value);
}
static void choose(void)
{
    DWORD start;
    int index = -1;
    /* A short date of the user's, stored while English (United States) is
       the locale: the value SetLocaleInfoA leaves under the key in Windows,
       without the ones it derives from it (those are the locale's own). */
    {
        HKEY key;
        DWORD done = !RegCreateKeyA(HKEY_CURRENT_USER, "Control Panel\\International", &key);
        if (done) {
            done = !RegSetValueExA(key, "sShortDate", 0, REG_SZ, (const BYTE *)"M/d/yyyy", 9);
            RegCloseKey(key);
        }
        number("set short date", done);
    }
    WinExec("RUNDLL32.EXE SHELL32.DLL,Control_RunDLL INTL.CPL", SW_SHOWNORMAL);
    for (start = GetTickCount(); !sheet && GetTickCount() - start < 60000;) {
        Sleep(200);
        EnumWindows(windows, 0);
    }
    number("sheet", sheet != 0);
    if (!sheet)
        return;
    Sleep(1500);
    EnumChildWindows(sheet, combo, 0);
    /* The entry of German (Austrian), 0C07, found without a pointer of this
       process going to the other one: the list holds the installed locales
       by their names, sorted when the box sorts, else in the order of the
       enumeration. */
    if (locale_list) {
        char mine[100];
        int i;
        DWORD sorted = (DWORD)GetWindowLongA(locale_list, GWL_STYLE) & CBS_SORT;
        names_count = 0;
        EnumSystemLocalesA(name_of, LCID_INSTALLED);
        GetLocaleInfoA(0x0C07, LOCALE_SLANGUAGE, mine, sizeof mine);
        index = 0;
        for (i = 0; i < names_count; i++) {
            if (names_lcid[i] == 0x0C07) {
                if (!sorted)
                    break;
                continue;
            }
            if (!sorted || lstrcmpiA(names[i], mine) < 0)
                index++;
        }
        if (!sorted && i == names_count)
            index = -1;
        if (index >= 0) {
            SendMessageA(locale_list, CB_SETCURSEL, index, 0);
            SendMessageA(GetParent(locale_list), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(locale_list), CBN_SELCHANGE),
                         (LPARAM)locale_list);
        }
        number("sorted", sorted != 0);
        number("selected length", (DWORD)SendMessageA(locale_list, CB_GETLBTEXTLEN,
                                                       SendMessageA(locale_list, CB_GETCURSEL, 0, 0), 0));
        number("name length", lstrlenA(mine));
    }
    number("listed", locale_list ? (DWORD)SendMessageA(locale_list, CB_GETCOUNT, 0, 0) : 0);
    number("chosen", index >= 0);
    Sleep(1000);
    /* PSM_PRESSBUTTON with PSBTN_OK (3). */
    PostMessageA(sheet, WM_USER + 113, 3, 0);
    for (start = GetTickCount(); IsWindow(sheet) && GetTickCount() - start < 60000;) {
        Sleep(200);
        EnumWindows(windows, 0);
    }
    number("closed", !IsWindow(sheet));
    if (IsWindow(sheet)) {
        EnumWindows(visible, 0);
        put("windows", shown);
    }
    Sleep(1000);
    international();
    answers();
}
void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    const char *mode = line + lstrlenA(line);
    while (mode > line && mode[-1] != ' ')
        mode--;
    out = CreateFileA("C:\\REGLOC.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    if (!lstrcmpA(mode, "list"))
        list();
    else if (!lstrcmpA(mode, "choose"))
        choose();
    else {
        international();
        answers();
    }
    put("done", "1");
    CloseHandle(out);
    ExitProcess(0);
}
