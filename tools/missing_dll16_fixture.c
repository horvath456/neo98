/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for a 16-bit program whose library is missing,
   started from the 32-bit side. KRNL386 LoadModule shows nothing and fails
   with 2, so CreateProcessA answers FALSE with ERROR_FILE_NOT_FOUND. The
   original SHELL32 then asks KERNEL32 ordinal 91 (PK16FNF) for the file the
   loader missed and says "Cannot find %1. Windows needs this file to run
   %2." (string 1941h); ShellExecuteEx answers SE_ERR_DLLNOTFOUND
   with error 1157. Exit code zero means every check held; codes 4720 to
   4739. */
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
    char command[] = "D:\\GAME\\SCHNAUZ.EXE";
    SetLastError(0);
    check(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information) == 0, 4721);
    check(GetLastError() == 2, 4722);
    check(information[0] == 0 && information[1] == 0, 4723);

    /* SHELLEXECUTEINFOA as in tools/missing_dll_fixture.c, verb "open". */
    U execute[15] = {60};
    execute[3] = (U) "open";
    execute[4] = (U) "D:\\GAME\\SCHNAUZ.EXE";
    execute[7] = 1;
    SetLastError(0);
    check(ShellExecuteExA(execute) == 0, 4724);
    check(execute[8] == 32, 4725);
    check(GetLastError() == 1157, 4726);
    ExitProcess(failure);
}
