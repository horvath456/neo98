/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API char *lstrcpyA(char *, const char *);
API char *lstrcpynA(char *, const char *, U);
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
static char source[131073], target[131080];
static void check(int okay, U code) { if (!okay) ExitProcess(code); }
void mainCRTStartup(void)
{
    U i, n;
    for (i = 0; i < sizeof(source)-1; ++i) source[i] = 'A' + i % 23;
    source[sizeof(source)-1] = 0;
    SetLastError(0x9876);
    target[sizeof(source)] = '!';
    check(lstrcpyA(target, source) == target && GetLastError() == 0x9876, 70);
    for (i = 0; i < sizeof(source); ++i) check(target[i] == source[i], 71);
    check(target[sizeof(source)] == '!', 72);
    check(lstrcpyA(target, "") == target && !target[0], 73);
    const U counts[] = {0, 1, 2, 6656, 65536, 100001, 131073, 0xffffffff};
    for (n = 0; n < sizeof(counts)/sizeof(counts[0]); ++n) {
        for (i = 0; i < sizeof(target); ++i) target[i] = '!';
        SetLastError(0x9876);
        check(lstrcpynA(target, source, counts[n]) == target, 10+n);
        check(GetLastError() == 0x9876, 20+n);
        U copied = counts[n] < sizeof(source) ? counts[n] : sizeof(source);
        for (i = 0; i + 1 < copied; ++i) check(target[i] == source[i], 30+n);
        if (copied) check(target[copied-1] == 0, 40+n);
        check(target[copied] == '!', 50+n);
    }
    check(lstrcpynA(target, "", 100) == target && !target[0], 60);
    check(lstrcpynA(source, source, 6656) == source && !source[6655], 61);
    /* Original forward DWORD moves: each group observes earlier writes. */
    const char overlap[] = "abcdefghijklmno";
    for (i = 0; i < sizeof(overlap); ++i) target[i] = overlap[i];
    check(lstrcpynA(target+1, target, 12) == target+1, 62);
    const char expected[] = "aabcddfghhhh";
    for (i = 0; i < sizeof(expected); ++i) check(target[i] == expected[i], 63);
    ExitProcess(0);
}
