/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for a program whose import names a DLL that does not exist.
   Windows 98 answers CreateProcessA with FALSE and ERROR_DLL_NOT_FOUND, the
   exit code the child's start routine ends with (KERNEL32). The original SHELL32 maps that to SE_ERR_DLLNOTFOUND and puts
   up no box of its own, because KERNEL32 already showed "A
   required.DLL file, %s, was not found." under "Error Starting Program".
   Exit code zero means every check held; codes 4700 to 4719. */
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
    char command[] = "\"D:\\NODLL32.EXE\" ";
    SetLastError(0);
    check(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information) == 0, 4701);
    check(GetLastError() == 1157, 4702);
    check(information[0] == 0 && information[1] == 0, 4703);

    /* SHELLEXECUTEINFOA: cbSize, fMask, hwnd, lpVerb, lpFile, lpParameters,
       lpDirectory, nShow, hInstApp, lpIDList, lpClass, hkeyClass, dwHotKey,
       hIcon, hProcess. */
    /* The verb "open", as the default command of Explorer's context menu
       passes it. Without a verb SHELL32 goes through that menu
       itself, and turns its E_FAIL into error 5. */
    U execute[15] = {60};
    execute[3] = (U) "open";
    execute[4] = (U) "D:\\NODLL32.EXE";
    execute[7] = 1;
    SetLastError(0);
    check(ShellExecuteExA(execute) == 0, 4704);
    check(execute[8] == 32, 4705);
    check(GetLastError() == 1157, 4706);
    ExitProcess(failure);
}
