/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetUserDefaultLCID(void);
API U InvalidateNLSCache(void);
API U RegCreateKeyExA(U, const char *, U, char *, U, U, void *, U *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegDeleteValueA(U, const char *);
API U RegCloseKey(U);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
void run_default_locale_test(void)
{
    /* Original NLS startup already populated the cache. This fixture exercises
       the separate public getter's cache-miss path after explicit invalidation. */
    check(InvalidateNLSCache() == 1, 3857);
    U key = 0, disposition = 0;
    check(!RegCreateKeyExA(0x80000001, "Control Panel\\International", 0, 0, 0, 0, 0, &key, &disposition),
          3850);
    const char initial[] = "00000407", changed[] = "00000409";
    check(!RegSetValueExA(key, "Locale", 0, 1, initial, sizeof initial), 3851);
    check(GetUserDefaultLCID() == 0x407, 3852);
    check(!RegSetValueExA(key, "Locale", 0, 1, changed, sizeof changed), 3853);
    /* The original getter keeps its cached nonzero value until another system
       path explicitly refreshes it. It must not silently reread each call. */
    check(GetUserDefaultLCID() == 0x407, 3854);
    check(!RegDeleteValueA(key, "Locale") && !RegCloseKey(key), 3855);
    check(GetUserDefaultLCID() == 0x407, 3856);
    check(InvalidateNLSCache() == 1 && GetUserDefaultLCID() == 0x409, 3858);
}
