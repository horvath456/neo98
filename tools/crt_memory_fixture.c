/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define CRT __declspec(dllimport) __cdecl
#define API __declspec(dllimport) __stdcall
CRT void *malloc(U);
CRT void *calloc(U, U);
CRT void *realloc(void *, U);
CRT void free(void *);
CRT U _msize(void *);
CRT int *_errno(void);
CRT void *memset(void *, int, U);
CRT void *memcpy(void *, const void *, U);
CRT void *memmove(void *, const void *, U);
CRT int memcmp(const void *, const void *, U);
CRT U __dllonexit(U, U *, U *);
CRT char *strrchr(const char *, int);
CRT unsigned char *_mbsrchr(const unsigned char *, U);
CRT unsigned char *_mbschr(const unsigned char *, U);
CRT char *strchr(const char *, int);
CRT unsigned char *_mbsinc(const unsigned char *);
CRT char *_strdup(const char *);
CRT int _setmbcp(int);
CRT int _getmbcp(void);
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U cleanup_order;
static void __cdecl cleanup_a(void) { cleanup_order = cleanup_order * 10 + 1; }
static void __cdecl cleanup_b(void) { cleanup_order = cleanup_order * 10 + 2; }
static U __stdcall child(U parent_errno)
{
    check((U)_errno() != parent_errno && !*_errno(), 2800);
    *_errno() = 22;
    return 0;
}
void run_crt_memory_test(void)
{
    SetLastError(98);
    unsigned char *p = malloc(4100);
    check(p && GetLastError() == 98 && _msize(p) >= 4100, 2801);
    p[0] = 12;
    p[4099] = 34;
    check(memset(p + 1, 0x198, 4098) == p + 1 && p[0] == 12 && p[4099] == 34, 2802);
    for (U i = 1; i < 4099; ++i)
        check(p[i] == 0x98, 2803);
    unsigned char *copy = calloc(4100, 2);
    check(copy != 0, 2804);
    for (U i = 0; i < 8200; ++i)
        check(!copy[i], 2805);
    check(memcpy(copy, p, 4100) == copy && copy[0] == 12 && copy[4099] == 34, 2806);
    check(memmove(copy + 1, copy, 4100) == copy + 1 && copy[1] == 12 && copy[4100] == 34, 2807);
    check(memmove(copy, copy + 1, 4100) == copy && copy[0] == 12 && copy[4099] == 34, 2808);
    unsigned char *grown = realloc(p, 8200);
    check(grown && grown[0] == 12 && grown[4099] == 34, 2809);
    check(!realloc(grown, ~0u) && *_errno() == 12 && grown[4099] == 34, 2810);
    check(!calloc(0x10000000, 16) && *_errno() == 12, 2811);
    U thread = CreateThread(0, 0, (U)child, (U)_errno(), 0, 0);
    check(thread && !WaitForSingleObject(thread, 10000) && CloseHandle(thread) && *_errno() == 12, 2812);
    check(!realloc(grown, 0), 2813);
    free(copy);
    free(0);
    p = realloc(0, 0);
    check(p != 0, 2814);
    free(p);
    *_errno() = 98;
    SetLastError(0x9898);
    check(!_msize(p) && *_errno() == 98 && GetLastError() == 87, 2833);
    check(!memset(0, 98, (0)) && !memcpy(0, 0, 0) && !memmove(0, 0, 0), 2815);
    U begin = (U)malloc(16), end = begin;
    check(begin, 2816);
    for (U i = 0; i < 10; ++i)
    {
        U function = i & 1 ? (U)cleanup_b : (U)cleanup_a;
        check(__dllonexit(function, &begin, &end) == function && end - begin == (i + 1) * 4, 2817);
    }
    check(_msize((void *)begin) >= 40, 2818);
    while (end != begin)
    {
        end -= 4;
        ((void(__cdecl *)(void))(*(U *)end))(); /* Owning DLL's detach loop. */
    }
    check(cleanup_order == 2121212121u, 2819);
    free((void *)begin);
    *_errno() = 98;
    const int pages[] = {0, -3, -2, -4, 1252, 437, 850};
    for (U i = 0; i < sizeof pages / sizeof *pages; ++i)
        check(!_setmbcp(pages[i]) && !_getmbcp() && *_errno() == 98, 2831);
    check(!_setmbcp(932) && _getmbcp() == 932 && *_errno() == 98, 2832);
    check(!_setmbcp(-3), 2833);
    const unsigned char path[] = "C:\\WINDOWS\\TEST.EXE";
    check(_mbsrchr(path, '\\') == path + 10 && _mbsinc(path + 10) == path + 11, 2820);
    check(_mbsrchr(path, 0x15c) == path + 10 && !_mbsrchr(path, '?'), 2821);
    check(strrchr((const char *)path, 0) == (const char *)path + sizeof path - 1, 2822);
    check(_mbschr(path, '\\') == path + 2 && _mbschr(path, 0x15c) == path + 2, 2840);
    check(_mbschr(path, 0) == path + sizeof path - 1 && !_mbschr(path, '?'), 2841);
    /* Search exceeds the old ReadString scratch buffer, including every initial alignment. */
    unsigned char *long_string = malloc(12004);
    check(long_string != 0, 2842);
    for (U i = 0; i < 12003; ++i)
        long_string[i] = 'a';
    long_string[12002] = 'z';
    long_string[12003] = 0;
    for (U offset = 0; offset < 4; ++offset)
    {
        check(strchr((const char *)long_string + offset, 'z') == (const char *)long_string + 12002, 2843);
        check(_mbschr(long_string + offset, 0) == long_string + 12003, 2844);
        check(strrchr((const char *)long_string + offset, 'a') == (const char *)long_string + 12001, 2845);
    }
    free(long_string);
    check(_mbsinc(path + sizeof path - 1) == path + sizeof path, 2823);
    char *duplicate = _strdup((const char *)path);
    check(duplicate && duplicate != (const char *)path && _msize(duplicate) >= sizeof path, 2824);
    for (U i = 0; i < sizeof path; ++i)
        check((unsigned char)duplicate[i] == path[i], 2825);
    duplicate[0] = 'D';
    check(path[0] == 'C', 2826);
    check(!memcmp(duplicate + 1, path + 1, sizeof path - 1), 2828);
    check(memcmp(duplicate, path, sizeof path) == 1 && memcmp(path, duplicate, sizeof path) == -1, 2829);
    check(!memcmp(0, 0, 0), 2830);
    free(duplicate);
    check(!_strdup(0), 2827);
}
