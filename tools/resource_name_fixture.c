/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_resource_name_guest.py: FindResourceA finds a named
   resource without regard to the case the caller spells it in, the way
   KERNEL32 does through the folding compare. The module is a
   file of the imported Win98 tree, its named resource is passed in by the test.
   Every check has its own exit code, so the runner names the failing one. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U FreeLibrary(U);
API U FindResourceA(U, const char *, const char *);
API U LoadResource(U, U);
API U SizeofResource(U, U);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
int mainCRTStartup(void)
{
    const U module = LoadLibraryA(RESOURCE_MODULE);
    check(module != 0, 1);
    const char *const type = (const char *)RESOURCE_TYPE;
    /* The upper case spelling is the one the resource directory stores. */
    const U upper = FindResourceA(module, RESOURCE_UPPER, type);
    check(upper != 0, 2);
    const U size = SizeofResource(module, upper);
    check(size != 0, 3);
    check(LoadResource(module, upper) != 0, 4);
    /* The same entry, spelled in lower case and in mixed case. */
    const U lower = FindResourceA(module, RESOURCE_LOWER, type);
    check(lower == upper, 5);
    const U mixed = FindResourceA(module, RESOURCE_MIXED, type);
    check(mixed == upper, 6);
    check(SizeofResource(module, lower) == size, 7);
    /* The negative probe: folding case must not fold anything else away. A
       name that differs in a letter, one that is a prefix and one that is
       longer stay unknown, and LastError is ERROR_RESOURCE_NAME_NOT_FOUND. */
    SetLastError(0);
    check(FindResourceA(module, RESOURCE_WRONG, type) == 0, 8);
    check(GetLastError() == 1814, 9);
    check(FindResourceA(module, RESOURCE_PREFIX, type) == 0, 10);
    check(FindResourceA(module, RESOURCE_LONGER, type) == 0, 11);
    /* A name is not a number: the decimal form of a named entry stays unknown. */
    check(FindResourceA(module, "#1", type) == 0, 12);
    check(FreeLibrary(module) != 0, 13);
    ExitProcess(0);
    return 0;
}
