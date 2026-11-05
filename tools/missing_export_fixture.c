/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for a program whose import names an export its DLL does not
   have, DDRAW.DLL!DirectDrawCreateEx on the DirectX 6.1 of the ISO
   installation. Windows 98 answers CreateProcessA with FALSE and
   ERROR_GEN_FAILURE (31), set by KERNEL32 together with the text of
   string 8. The original SHELL32 has no special case for 31 and shows its own
   box with the system text of the error. Exit code zero means every check
   held; codes 4720 to 4739. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U ShellExecuteExA(U *);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}

void mainCRTStartup(void)
{
    U startup[17] = {68};
    U information[4] = {0};
    char command[] = "\"D:\\NOEXPORT.EXE\" ";
    SetLastError(0);
    check(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information) == 0, 4721);
    check(GetLastError() == 31, 4722);
    check(information[0] == 0 && information[1] == 0, 4723);

    U execute[15] = {60};
    execute[3] = (U) "open";
    execute[4] = (U) "D:\\NOEXPORT.EXE";
    execute[7] = 1;
    SetLastError(0);
    check(ShellExecuteExA(execute) == 0, 4724);
    check(GetLastError() == 31, 4725);
    ExitProcess(failure);
}
