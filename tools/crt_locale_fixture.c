/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U GetProcAddress(U, const char *);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U GetLocaleInfoA(U, U, void *, U);
API U GetLastError(void);
API U LocalAlloc(U, U);
API U LocalFree(U);
CRT char *setlocale(int, const char *);
CRT char **__p__acmdln(void);
CRT unsigned short **__p__pctype(void);
CRT unsigned short **__p__pwctype(void);
CRT int *__p___mb_cur_max(void);
CRT int _setmbcp(int);
CRT int _getmbcp(void);
CRT U _isctype(U, U);
CRT U toupper(U);
CRT U tolower(U);
CRT U _onexit(U);
CRT void _cexit(void);
typedef struct
{
    char *decimal, *thousands, *grouping, *international, *currency, *money_decimal, *money_thousands,
        *money_grouping, *positive, *negative;
    unsigned char digits[8];
} Conv;
CRT Conv *localeconv(void);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static U __stdcall worker(U unused)
{
    (void)unused;
    check(equal(setlocale(2, 0), "English_United States.1252"), 3260);
    check(equal(setlocale(4, "C"), "C"), 3261);
    return 98;
}
static U cleanup_armed, cleanup_calls;
static volatile U cleanup_worker_done;
static U __stdcall cleanup_worker(U unused)
{
    (void)unused;
    check(equal(setlocale(2, ""), "English_United States.1252"), 3480);
    cleanup_worker_done = 1;
    return 98;
}
static void __cdecl cleanup_callback(void)
{
    /* Win98 _cexit retains this registration. Later CRT-exit tests must see
       a no-op, rather than creating another worker during process teardown. */
    if (!cleanup_armed)
        return;
    cleanup_armed = 0;
    ++cleanup_calls;
    /* Creation inside the callback guarantees the exit lock is already held
       when the worker first tries the separate locale lock. */
    U thread = CreateThread(0, 0, (U)cleanup_worker, 0, 0, 0);
    check(thread != 0, 3481);
    check(!WaitForSingleObject(thread, 10000), 3482);
    check(cleanup_worker_done && CloseHandle(thread), 3483);
}
void run_crt_locale_heap_test(void)
{
    /* The arena grows up to 256 MiB, so the array holds more than
       fit before the second pass runs dry. */
    static U blocks[1024];
    U count = 0;
    /* Called first, before any guest locale table has been published. Fill the
       arena in two passes without relying on its exact available capacity. */
    for (U phase = 0; phase < 2; ++phase)
    {
        while (count < 1024)
        {
            U block = LocalAlloc(0, phase ? 8192 : 1024 * 1024);
            if (!block)
                break;
            blocks[count++] = block;
        }
        check(count < 1024, 3470);
    }
    check(!localeconv(), 3471);
    check(toupper('a') == 'A', 3472);
    check(tolower('Z') == 'z', 3473);
    check(_isctype('a', 2) == 2, 3474);
    while (count)
        check(!LocalFree(blocks[--count]), 3475);
    Conv *c = localeconv();
    check(c && equal(c->decimal, ".") && !*c->currency, 3476);
}
void run_crt_locale_test(void)
{
    check(equal(setlocale(0, 0), "C"), 3262);
    check(!setlocale(-1, "C") && !setlocale(6, "C"), 3263);
    Conv *c = localeconv();
    check(c && equal(c->decimal, ".") && !*c->currency && !*c->grouping, 3264);
    for (U i = 0; i < 8; ++i)
        check(c->digits[i] == 127, 3265);
    unsigned short **ctype = __p__pctype(), **wide = __p__pwctype();
    check(ctype && wide && ((*ctype)['a'] & 2) && !(*ctype)[0xe9] && !(*ctype)[-1], 3266);
    check(equal(setlocale(0, ""), "English_United States.1252"), 3267);
    check((*ctype)[0xe9] & 2, 3268);
    check((*wide)[0xe9] & 2, 3269);
    check(toupper(0xe9) == 0xc9 && tolower(0xc9) == 0xe9 && _isctype(0xe9, 2), 3287);
    check(*__p___mb_cur_max() == 1 && !_setmbcp(-4) && !_getmbcp(), 3270);
    c = localeconv();
    check(equal(c->decimal, ".") && equal(c->thousands, ",") && c->grouping[0] == 3 && !c->grouping[1], 3271);
    check(equal(c->currency, "$") && equal(c->international, "USD"), 3272);
    check(equal(c->money_decimal, ".") && equal(c->money_thousands, ","), 3273);
    check(c->money_grouping[0] == 3 && !c->money_grouping[1] && c->digits[0] == 2 && c->digits[1] == 2, 3274);
    U dll = GetModuleHandleA("MSVCRT.DLL");
    check(GetProcAddress(dll, "_acmdln") == (U)__p__acmdln() && **__p__acmdln(), 3297);
    check(GetProcAddress(dll, "_pctype") == (U)ctype && GetProcAddress(dll, "_pwctype") == (U)wide, 3275);
    U *page = (U *)GetProcAddress(dll, "__lc_codepage"), *handles = (U *)GetProcAddress(dll, "__lc_handle");
    check(page && *page == 1252 && handles && handles[2] == 0x409, 3276);
    unsigned short *classic = (unsigned short *)GetProcAddress(dll, "_ctype");
    check(classic && !classic[0xea] && (classic['a' + 1] & 2), 3288);
    check(!setlocale(0, "Unsupported_Country.9999") && *page == 1252, 3277);
    check(equal(setlocale(2, "C"), "C") && !*page && !(*ctype)[0xe9], 3278);
    check(equal(setlocale(0, 0),
                "LC_COLLATE=English_United States.1252;LC_CTYPE=C;LC_MONETARY=English_United "
                "States.1252;LC_NUMERIC=English_United States.1252;LC_TIME=English_United States.1252"),
          3279);
    check(setlocale(0, "LC_CTYPE=English_United States.1252;LC_NUMERIC=C") != 0, 3280);
    check(*page == 1252 && !*localeconv()->grouping && equal(localeconv()->currency, "$"), 3281);
    check(equal(setlocale(4, ""), "English_United States.1252"), 3282);
    U thread = CreateThread(0, 0, (U)worker, 0, 0, 0);
    check(thread && !WaitForSingleObject(thread, 10000) && CloseHandle(thread), 3283);
    check(equal(setlocale(4, 0), "C") && !*localeconv()->grouping, 3284);
    check(equal(setlocale(0, "C"), "C") && !*page, 3285);
    check(!*localeconv()->currency && !_setmbcp(-4) && !_getmbcp(), 3286);
    check(toupper(0xe9) == 0xe9 && !_isctype(0xe9, 2) && _isctype(0x1e9, 2), 3289);
    char text[128];
    check(GetLocaleInfoA(0x409, 0x1004, text, sizeof text) == 5 && equal(text, "1252"), 3290);
    check(GetLocaleInfoA(0x400, 0x80000014, text, sizeof text) == 2 && equal(text, "$"), 3291);
    U number = 0;
    check(GetLocaleInfoA(0x409, 0x20000001, &number, 4) == 4 && number == 0x409, 3292);
    check(GetLocaleInfoA(0x409, 0x20001004, &number, 4) == 4 && number == 1252, 3293);
    check(GetLocaleInfoA(0x409, 0x1004, 0, 0) == 5, 3294);
    number = 0x98989898;
    check(!GetLocaleInfoA(0x409, 0x1004, &number, 4) && GetLastError() == 122 && number == 0x98989898, 3295);
    check(!GetLocaleInfoA(0x409, 0x20000014, 0, 0) && GetLastError() == 1004, 3296);
    /* common_fixture.c runs this before registering the separate exit tests. */
    cleanup_armed = 1;
    check(_onexit((U)cleanup_callback) == (U)cleanup_callback, 3484);
    _cexit();
    check(!cleanup_armed && cleanup_calls == 1 && cleanup_worker_done, 3485);
    check(*page == 1252 && equal(setlocale(2, 0), "English_United States.1252"), 3486);
    check(equal(setlocale(0, "C"), "C") && !*page, 3487);
    _cexit(); /* Confirm the retained callback is inert for subsequent cleanup. */
    check(cleanup_calls == 1 && !*page && equal(setlocale(0, 0), "C"), 3488);
}
