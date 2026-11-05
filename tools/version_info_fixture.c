/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_version_info_guest.py: the public 32-bit version
   API of the original VERSION.DLL against a file of the imported Win98 tree
   whose version resource the test reads with pefile. Every check has its own
   exit code, so the runner names the failing one. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetFileVersionInfoSizeA(const char *, U *);
API U GetFileVersionInfoA(const char *, U, U, void *);
API U VerQueryValueA(const void *, const char *, void **, U *);
API U VerLanguageNameA(U, char *, U);
API void ExitProcess(U);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static U length(const char *a)
{
    U n = 0;
    while (a[n])
        ++n;
    return n;
}
static unsigned char block[16384];
static char name[128];
void __stdcall mainCRTStartup(void)
{
    U handle = 0x12345678, size = 0, bytes = 0;
    void *value = 0;
    const U *fixed;
    const unsigned short *translation;

    size = GetFileVersionInfoSizeA(VERSION_FILE, &handle);
    check(size != 0, 1);
    check(size <= sizeof block, 2);
    check(GetFileVersionInfoA(VERSION_FILE, handle, sizeof block, block) != 0, 3);

    check(VerQueryValueA(block, "\\", &value, &bytes) != 0, 4);
    check(bytes == 52, 5);
    fixed = (const U *)value;
    check(fixed[0] == 0xfeef04bdu, 6);
    check(fixed[2] == VERSION_MS, 7);
    check(fixed[3] == VERSION_LS, 8);
    check(fixed[9] == VERSION_TYPE, 9);

    check(VerQueryValueA(block, "\\VarFileInfo\\Translation", &value, &bytes) != 0, 10);
    check(bytes >= 4 && (bytes & 3) == 0, 11);
    translation = (const unsigned short *)value;
    check(translation[0] == VERSION_LANGUAGE, 12);
    check(translation[1] == VERSION_CODEPAGE, 13);

    check(VerQueryValueA(block, VERSION_COMPANY_KEY, &value, &bytes) != 0, 14);
    check(equal((const char *)value, VERSION_COMPANY), 15);
    check(bytes == length(VERSION_COMPANY) + 1, 16);

    /* A path that names nothing must fail, and the answer must survive a
       leading and a doubled backslash the way the original collapses them. */
    check(VerQueryValueA(block, "\\StringFileInfo\\040904E4\\NoSuchValue", &value, &bytes) == 0, 17);
    check(VerQueryValueA(block, "\\\\VarFileInfo\\\\Translation\\", &value, &bytes) != 0, 18);
    check(VerQueryValueA(0, "\\", &value, &bytes) == 0, 19);

    /* VerLanguageName answers the untruncated length and truncates the copy. */
    check(VerLanguageNameA(0x0409, name, sizeof name) == 23, 20);
    check(equal(name, "English (United States)"), 21);
    check(VerLanguageNameA(0x0407, name, sizeof name) == 17, 22);
    check(equal(name, "German (Standard)"), 23);
    check(VerLanguageNameA(0xfffe, name, sizeof name) == 16, 24);
    check(equal(name, "Language Neutral"), 25);
    check(VerLanguageNameA(0x0409, name, 8) == 23, 26);
    check(equal(name, "English"), 27);
    ExitProcess(0);
}
