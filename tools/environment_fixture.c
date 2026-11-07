/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 client checks original Win98 error returns and the shared ANSI environment. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API void *GetEnvironmentStringsW(void);
API char *GetEnvironmentStrings(void);
API char *GetEnvironmentStringsA(void);
API U FreeEnvironmentStringsA(char *);
API U FreeEnvironmentStringsW(void *);
API U CreateNamedPipeA(const char *, U, U, U, U, U, U, void *);
API U CreateTapePartition(U, U, U, U);
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
void run_environment_test(void)
{
    SetLastError(98);
    check(!GetEnvironmentStringsW() && GetLastError() == 120, 780);
    check(!FreeEnvironmentStringsW(0) && GetLastError() == 120, 781);
    check(CreateNamedPipeA(0, 0, 0, 0, 0, 0, 0, 0) == ~0u && GetLastError() == 120, 782);
    check(CreateTapePartition(0, 0, 0, 0) == 1 && GetLastError() == 120, 783);
    char *block = GetEnvironmentStringsA();
    check(block != 0 && GetEnvironmentStrings() == block && GetEnvironmentStringsA() == block, 784);
    U count = 0, have_temp = 0, have_windir = 0, have_comspec = 0;
    for (const char *p = block; *p;)
    {
        ++count;
        have_comspec |= equal(p, "COMSPEC=C:\\WINDOWS\\COMMAND.COM");
        have_temp |= equal(p, "TEMP=C:\\WINDOWS\\TEMP");
        have_windir |= equal(p, "windir=C:\\WINDOWS");
        while (*p)
            ++p;
        ++p;
    }
    check(count == 5 && have_temp && have_windir && have_comspec, 785);
    check(!FreeEnvironmentStringsA(block + 1) && GetLastError() == 87, 786);
    SetLastError(98);
    check(FreeEnvironmentStringsA(block) && GetLastError() == 98, 787);
    check(GetEnvironmentStringsA() == block && FreeEnvironmentStringsA(block), 788);
    check(!FreeEnvironmentStringsA(0) && GetLastError() == 87, 789);
}
