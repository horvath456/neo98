/* SPDX-License-Identifier: GPL-2.0-or-later
 * What a process finds when it starts: a return address inside KERNEL32, the
 * image of KERNEL32 at its Win98, and an export directory a program can
 * walk by itself. Unpackers do exactly this (ASPack in Collapse.exe). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U GetProcAddress(U, const char *);
API void ExitProcess(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}
/* The export walk of an unpacker: names, ordinals, functions, base + RVA. */
static U walk(U base, const char *wanted)
{
    const unsigned char *image = (const unsigned char *)base;
    const U pe = *(const U *)(image + 0x3c);
    const U directory = *(const U *)(image + pe + 0x78);
    const U *exports = (const U *)(base + directory);
    const U names = exports[6], *functions = (const U *)(base + exports[7]);
    const U *name = (const U *)(base + exports[8]);
    const unsigned short *ordinal = (const unsigned short *)(base + exports[9]);
    for (U i = 0; i < names; ++i)
        if (same((const char *)(base + name[i]), wanted))
            return base + functions[ordinal[i]];
    return 0;
}
U __stdcall mainCRTStartup(void)
{
    const U back = (U)__builtin_return_address(0);
    /* ASPack gives up below this address; KERNEL32 of Win98 lies. */
    check(back >= 0x77e00000u, 5101);
    U base = (back & 0xffff0000u) + 0x10000;
    U steps = 0;
    do
    {
        base -= 0x10000;
        check(++steps <= 16, 5102);
    } while (*(const unsigned short *)base != 0x5a4d);
    check(base == 0xbff70000u, 5103);
    check(base == GetModuleHandleA("KERNEL32.DLL"), 5104);
    const U found = walk(base, "GetProcAddress");
    check(found && found == GetProcAddress(base, "GetProcAddress"), 5105);
    typedef U(__stdcall * Lookup)(U, const char *);
    check(((Lookup)found)(base, "LoadLibraryA") == walk(base, "LoadLibraryA"), 5106);
    check(walk(base, "LoadLibraryA") != 0 && !walk(base, "NoSuchExport"), 5107);
    /* A program that returns from its entry ends with the returned code. */
    return 0;
}
