/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CCAlloc(U);
API U CCReAlloc(U, U);
API U CCFree(U);
API const char *CCStrChrA(const char *, U);
API U CCStrToIntA(const char *);
API U CCStrToIntW(const unsigned short *);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
void run_string_helpers_test(void)
{
    const char text[] = "NEO98: 123";
    check(CCStrChrA(text, ':') == text + 5, 1700);
    check(!CCStrChrA(text, 0) && !CCStrChrA(text, 'z'), 1701);
    check(CCStrChrA(text, 0xabba0039) == text + 3, 1702);
    check(CCStrToIntA("-2147483649suffix") == 0x7fffffff, 1703);
    check(CCStrToIntA("4294967394") == 98, 1704);
    check(!CCStrToIntA("+98") && !CCStrToIntA(" 98") && !CCStrToIntA(0), 1705);
    const char extended[] = {'9', '8', (char)0x80, 0};
    check(CCStrToIntA(extended) == 98, 1706);
    const unsigned short wide[] = {'-', '9', '8', 0x3a9, 0};
    check(CCStrToIntW(wide) == (U)-98, 1707);
    for (U i = 0; i < 100; ++i)
        check(CCStrToIntA("1234567890tail") == 1234567890, 1708);
    U p = CCAlloc(33);
    check(p != 0, 1709);
    for (U i = 0; i < 33; ++i)
    {
        check(!((unsigned char *)p)[i], 1710);
        ((unsigned char *)p)[i] = i + 1;
    }
    p = CCReAlloc(p, 120);
    check(p != 0, 1711);
    for (U i = 0; i < 33; ++i)
        check(((unsigned char *)p)[i] == i + 1, 1712);
    for (U i = 48; i < 120; ++i)
        check(!((unsigned char *)p)[i], 1713);
    check(!CCReAlloc(p, 0xffffffff), 1714);
    check(((unsigned char *)p)[32] == 33, 1715);
    check(CCFree(p) && !CCFree(p), 1716);
    p = CCReAlloc(0, 20);
    check(p && CCFree(p) && !CCFree(0), 1717);
}
