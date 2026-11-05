/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CLI and STI in a Win32 program go on: the VMM virtualizes the
   interrupt flag of a protected-mode application. MSO.DLL of Office XP uses
   them around its compare-exchange for Windows 95 (30B25194). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
static U __attribute__((noinline)) exchange(volatile U *p, U value, U comparand)
{
    U old;
    __asm__ volatile("cli");
    old = *p;
    if (old == comparand) *p = value;
    __asm__ volatile("sti");
    return old;
}
void mainCRTStartup(void)
{
    volatile U word = 5;
    if (exchange(&word, 9, 5) != 5 || word != 9) ExitProcess(1);
    if (exchange(&word, 1, 5) != 9 || word != 9) ExitProcess(2);
    ExitProcess(0);
}
