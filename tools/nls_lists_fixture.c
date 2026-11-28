/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * The lists of KERNEL32's NLS functions (tools/test_nls_lists_host.py):
 *
 *   cp-installed= / cp-supported=   EnumSystemCodePagesA with CP_INSTALLED
 *                                   and CP_SUPPORTED, in the order given
 *   first=                          IsValidLocale (installed) of three
 *                                   locales before any other is loaded in
 *                                   the process, and the English name of
 *                                   Hindi
 *   cp-valid=                       IsValidCodePage for a set of pages
 *   locale XXXXXXXX|...             for each locale of the registry (the
 *                                   values of Nls\Locale and Nls\AddLocales)
 *                                   and a few outside it: IsValidLocale with
 *                                   LCID_INSTALLED and LCID_SUPPORTED, and
 *                                   the names of its language and its country
 *                                   (GetLocaleInfoA: the length of the
 *                                   answer and its text, or "-" and the
 *                                   error)
 *
 * The lines go into C:\NLSLIST.TXT and, when it opens, to COM1; the last is
 * MEASURE_DONE (the same program measures the real Windows under QEMU).
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, serial = INVALID_HANDLE_VALUE;
static void put(const char *s)
{
    DWORD n;
    WriteFile(out, s, lstrlenA(s), &n, 0);
    if (serial != INVALID_HANDLE_VALUE)
        WriteFile(serial, s, lstrlenA(s), &n, 0);
}

static char list[4096];
static BOOL CALLBACK page(LPSTR name)
{
    if (lstrlenA(list) + lstrlenA(name) + 2 < (int)sizeof list) {
        lstrcatA(list, " ");
        lstrcatA(list, name);
    }
    return TRUE;
}

static DWORD lcids[400];
static int lcid_count;
static void add(DWORD lcid)
{
    int i;
    for (i = 0; i < lcid_count; i++)
        if (lcids[i] == lcid)
            return;
    if (lcid_count < 400)
        lcids[lcid_count++] = lcid;
}
static void registry(const char *path)
{
    HKEY key;
    char name[300], data[300];
    DWORD i, n, d, type, lcid;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key))
        return;
    for (i = 0;; i++) {
        int j, good;
        n = sizeof name;
        d = sizeof data;
        if (RegEnumValueA(key, i, name, &n, 0, &type, (BYTE *)data, &d))
            break;
        good = lstrlenA(name) == 8;
        lcid = 0;
        for (j = 0; good && j < 8; j++) {
            char c = name[j];
            int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            good = v >= 0;
            lcid = lcid << 4 | (v & 15);
        }
        if (good)
            add(lcid);
    }
    RegCloseKey(key);
}

static const struct { const char *name; LCTYPE type; } fields[] = {
    { "SLANGUAGE", LOCALE_SLANGUAGE },
    { "SENGLANGUAGE", LOCALE_SENGLANGUAGE },
    { "SNATIVELANGNAME", LOCALE_SNATIVELANGNAME },
    { "SABBREVLANGNAME", LOCALE_SABBREVLANGNAME },
    { "SCOUNTRY", LOCALE_SCOUNTRY },
    { "SENGCOUNTRY", LOCALE_SENGCOUNTRY },
    { "SNATIVECTRYNAME", LOCALE_SNATIVECTRYNAME },
    { "SABBREVCTRYNAME", LOCALE_SABBREVCTRYNAME },
    { "ICOUNTRY", LOCALE_ICOUNTRY },
};

void mainCRTStartup(void)
{
    static const DWORD pages[] = { 0, 1, 2, 37, 437, 708, 720, 737, 775, 850, 852, 855, 857, 860, 861, 862, 863,
                                   864, 865, 866, 869, 874, 932, 936, 949, 950, 1200, 1250, 1251, 1252, 1253,
                                   1254, 1255, 1256, 1257, 1258, 1361, 10000, 20866, 28591, 28592, 28593, 28594,
                                   28595, 28597, 65000, 65001 };
    static const DWORD extra[] = { 0, 0x400, 0x800, 0x7, 0x9, 0x409, 0x10409, 0x436, 0x401, 0x404, 0x40d, 0x411,
                                   0x412, 0x41e, 0x420, 0x429, 0x439, 0x42a, 0x804, 0x1004, 0xc04, 0x1404 };
    char line[2600], part[400], text[300];
    int i, j;
    out = CreateFileA("C:\\NLSLIST.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    serial = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);

    /* Before any other locale is loaded in this process. */
    i = GetLocaleInfoA(0x439, LOCALE_SENGLANGUAGE, text, sizeof text);
    wsprintfA(line, "first=240A:%d 3C0A:%d 439:%d,%d,%d\r\n", IsValidLocale(0x240a, LCID_INSTALLED) ? 1 : 0,
              IsValidLocale(0x3c0a, LCID_INSTALLED) ? 1 : 0, IsValidLocale(0x439, LCID_INSTALLED) ? 1 : 0,
              IsValidLocale(0x439, LCID_SUPPORTED) ? 1 : 0, i);
    put(line);

    list[0] = 0;
    SetLastError(0);
    i = EnumSystemCodePagesA(page, CP_INSTALLED);
    wsprintfA(line, "cp-installed=%d/%lu%s\r\n", i, GetLastError(), list);
    put(line);
    list[0] = 0;
    SetLastError(0);
    i = EnumSystemCodePagesA(page, CP_SUPPORTED);
    wsprintfA(line, "cp-supported=%d/%lu%s\r\n", i, GetLastError(), list);
    put(line);
    SetLastError(0);
    i = EnumSystemCodePagesA(page, 3);
    wsprintfA(line, "cp-both=%d/%lu\r\n", i, GetLastError());
    put(line);
    lstrcpyA(line, "cp-valid=");
    for (i = 0; i < (int)(sizeof pages / sizeof pages[0]); i++) {
        wsprintfA(part, "%s%lu:%d", i ? " " : "", pages[i], IsValidCodePage(pages[i]) ? 1 : 0);
        lstrcatA(line, part);
    }
    lstrcatA(line, "\r\n");
    put(line);

    registry("System\\CurrentControlSet\\Control\\Nls\\Locale");
    registry("System\\CurrentControlSet\\Control\\Nls\\AddLocales");
    for (i = 0; i < (int)(sizeof extra / sizeof extra[0]); i++)
        add(extra[i]);
    for (i = 0; i < lcid_count; i++) {
        DWORD lcid = lcids[i];
        wsprintfA(line, "locale %08lX|inst %d|supp %d", lcid, IsValidLocale(lcid, LCID_INSTALLED) ? 1 : 0,
                  IsValidLocale(lcid, LCID_SUPPORTED) ? 1 : 0);
        for (j = 0; j < (int)(sizeof fields / sizeof fields[0]); j++) {
            int n;
            SetLastError(0);
            n = GetLocaleInfoA(lcid, fields[j].type, text, sizeof text);
            if (n)
                wsprintfA(part, "|%s %d %s", fields[j].name, n, text);
            else
                wsprintfA(part, "|%s - %lu", fields[j].name, GetLastError());
            lstrcatA(line, part);
        }
        lstrcatA(line, "\r\n");
        put(line);
    }
    put("done\r\n");
    put("MEASURE_DONE\r\n");
    CloseHandle(out);
    ExitProcess(0);
}
