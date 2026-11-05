/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetSystemDefaultLCID(void);
API U GetSystemDefaultLangID(void);
API U GetUserDefaultLCID(void);
API U GetUserDefaultLangID(void);
API U GetThreadLocale(void);
API U InvalidateNLSCache(void);
API U GetACP(void);
API U GetOEMCP(void);
API U GetLocaleInfoA(U, U, char *, U);
API void SetLastError(U);
API U GetLastError(void);
API U RegCreateKeyExA(U, const char *, U, char *, U, U, void *, U *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegDeleteValueA(U, const char *);
API U RegCloseKey(U);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U CloseHandle(U);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void set(U key, const char *text, U length)
{
    check(!RegSetValueExA(key, "Locale", 0, 1, text, length), 5100);
}
static U __stdcall worker(void *unused)
{
    (void)unused;
    check(GetSystemDefaultLCID() == 0x409 && GetThreadLocale() == 0x407, 5150);
    check(InvalidateNLSCache() == 1 && !GetThreadLocale(), 5151);
    check(GetUserDefaultLCID() == 0x407 && GetThreadLocale() == 0x407, 5152);
    return 98;
}
void run_system_locale_test(void)
{
    U key = 0, disposition = 0;
    check(GetSystemDefaultLCID() == 0x409 && GetSystemDefaultLangID() == 0x409, 5101);
    check(GetUserDefaultLCID() == 0x409 && GetUserDefaultLangID() == 0x409 && GetThreadLocale() == 0x409,
          5102);
    /* OEMCP comes from HKLM\\System\\CurrentControlSet\\Control\\Nls\\CodePage
       (original ). The ISO installation, set up with Locale=L0409,
       stores 437 there; the reference system installation this was first measured on
       stored 850. */
    check(GetACP() == 1252 && GetOEMCP() == 437, 5103);
    SetLastError(98);
    check(InvalidateNLSCache() == 1 && !GetThreadLocale(), 5104);
    check(GetSystemDefaultLCID() == 0x409 && GetSystemDefaultLangID() == 0x409 && GetLastError() == 98,
          5105);
    check(!GetThreadLocale() && GetUserDefaultLangID() == 0x409 && GetThreadLocale() == 0x409, 5106);
    check(!RegCreateKeyExA(0x80000001, "Control Panel\\International", 0, 0, 0, 0, 0, &key, &disposition),
          5107);
    const char quoted[] = "\"00000407\"", prefixed[] = "0X00000411", zero[] = "0", lost_nibble[] = "FFFFFFFF\"";
    set(key, quoted, sizeof quoted);
    check(InvalidateNLSCache() == 1 && !GetThreadLocale(), 5108);
    check(GetUserDefaultLCID() == 0x407 && GetUserDefaultLangID() == 0x407 && GetThreadLocale() == 0x407,
          5109);
    set(key, prefixed, sizeof prefixed);
    check(GetUserDefaultLCID() == 0x407 && GetThreadLocale() == 0x407, 5110);
    check(InvalidateNLSCache() == 1 && GetUserDefaultLCID() == 0x411 && GetThreadLocale() == 0x411, 5111);
    check(GetSystemDefaultLCID() == 0x409, 5112);
    char output[16];
    check(!GetLocaleInfoA(0x411, 1, output, sizeof output) && GetLastError() == 87, 5113);
    set(key, zero, sizeof zero);
    check(InvalidateNLSCache() == 1, 5114);
    SetLastError(98);
    check(!GetUserDefaultLCID() && !GetUserDefaultLangID() && !GetThreadLocale(), 5115);
    check(GetSystemDefaultLCID() == 0x409 && GetLastError() == 98, 5116);
    set(key, lost_nibble, sizeof lost_nibble);
    check(GetUserDefaultLCID() == 0x0fffffff && GetUserDefaultLangID() == 0xffff, 5117);
    check(GetThreadLocale() == 0x0fffffff && GetSystemDefaultLCID() == 0x409, 5118);
    const char invalid[] = " 409";
    set(key, invalid, sizeof invalid);
    check(InvalidateNLSCache() == 1 && !GetUserDefaultLCID() && !GetThreadLocale(), 5119);
    check(!RegDeleteValueA(key, "Locale"), 5120);
    check(GetUserDefaultLCID() == 0x409 && GetThreadLocale() == 0x409, 5121);
    U machine = 0;
    check(!RegCreateKeyExA(0x80000002, "SYSTEM\\CurrentControlSet\\Control\\Nls\\Locale", 0, 0, 0, 0, 0,
                          &machine, &disposition),
          5122);
    const char german[] = "00000407";
    check(!RegSetValueExA(machine, "", 0, 1, german, sizeof german), 5123);
    check(InvalidateNLSCache() == 1 && GetSystemDefaultLCID() == 0x409 && GetUserDefaultLCID() == 0x409,
          5124);
    check(!RegDeleteValueA(machine, "") && !RegCloseKey(machine), 5125);
    char oversized[129];
    for (U i = 0; i < sizeof oversized - 1; ++i)
        oversized[i] = '0';
    oversized[sizeof oversized - 1] = 0;
    set(key, oversized, sizeof oversized);
    check(InvalidateNLSCache() == 1 && GetUserDefaultLCID() == 0x409, 5126);
    const U wrong_type = 0x407;
    check(!RegSetValueExA(key, "Locale", 0, 4, &wrong_type, sizeof wrong_type), 5127);
    check(InvalidateNLSCache() == 1 && GetUserDefaultLCID() == 0x409, 5128);
    set(key, german, sizeof german);
    check(InvalidateNLSCache() == 1 && GetUserDefaultLCID() == 0x407, 5129);
    U thread_id = 0, code = 0;
    const U thread = CreateThread(0, 0, worker, 0, 0, &thread_id);
    check(thread && thread_id, 5130);
    check(WaitForSingleObject(thread, 0xffffffff) == 0 && GetExitCodeThread(thread, &code) && code == 98,
          5131);
    check(CloseHandle(thread) && GetThreadLocale() == 0x407 && GetSystemDefaultLCID() == 0x409, 5132);
    check(!RegDeleteValueA(key, "Locale") && !RegCloseKey(key), 5133);
    check(InvalidateNLSCache() == 1 && !GetThreadLocale() && GetUserDefaultLCID() == 0x409, 5134);
    /* The code pages come from the registry, not from the thread locale. */
    check(GetThreadLocale() == 0x409 && GetACP() == 1252 && GetOEMCP() == 437, 5135);
    /* LOCALE_SLANGUAGE comes out of KERNEL32's string table, the LANGID as
       the string number (; Regional Settings). */
    {
        static const char english[] = "English (United States)";
        char language[64];
        U i;
        check(GetLocaleInfoA(0x409, 2, 0, 0) == sizeof english, 5136);
        check(GetLocaleInfoA(0x409, 2, language, sizeof language) == sizeof english, 5137);
        for (i = 0; i < sizeof english; ++i) check(language[i] == english[i], 5138);
    }
}
