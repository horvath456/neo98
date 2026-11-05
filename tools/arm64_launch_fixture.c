/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An x86 program that starts ARM64 programs the two ways x86 code does:
   CreateProcessA, and ShellExecuteExA with the verb "open", which is what
   Explorer's double click runs through the original SHELL32. It waits for the
   console program and writes what it saw into LAUNCH.TXT beside itself.
   tools/test_arm64_start_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API U ShellExecuteExA(U *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
API int lstrlenA(const char *);

void mainCRTStartup(void)
{
    U startup[17] = {68}, information[4] = {0}, code = 0xdead, written = 0;
    char command[] = "C:\\HELLO.EXE one two";
    const U created = CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information);
    const U waited = created ? WaitForSingleObject(information[0], 20000) : 0xffff;
    if (created) GetExitCodeProcess(information[0], &code);
    /* SHELLEXECUTEINFOA; fMask 40h keeps the process handle. */
    U execute[15] = {60, 0x40};
    execute[3] = (U) "open";
    execute[4] = (U) "C:\\MINES.EXE";
    execute[7] = 1;
    const U shell = ShellExecuteExA(execute);
    char text[200];
    wsprintfA(text, "created=%u\r\nwaited=%u\r\nexit=%u\r\nshell=%u\r\ninstance=%u\r\nprocess=%u\r\n", created, waited,
              code, shell, execute[8] > 32, execute[14] != 0);
    const U file = CreateFileA("C:\\LAUNCH.TXT", 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
