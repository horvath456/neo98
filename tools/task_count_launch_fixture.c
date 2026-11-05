/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An x86 program that starts the Win16 program D:\NUMTASKS.EXE, waits for it
   and writes its exit code into D:\NUMTASKS.TXT. The Win16 program is then a
   child process, the case in which GetNumTasks once never returned.
   tools/test_win16_task_count_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
API int lstrlenA(const char *);

void mainCRTStartup(void)
{
    U startup[17] = {68}, information[4] = {0}, code = 0xdead, written = 0;
    char command[] = "D:\\NUMTASKS.EXE";
    const U created = CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information);
    const U waited = created ? WaitForSingleObject(information[0], 20000) : 0xffff;
    if (created) GetExitCodeProcess(information[0], &code);
    char text[100];
    wsprintfA(text, "created=%u\r\nwaited=%u\r\nexit=%u\r\n", created, waited, code);
    const U file = CreateFileA("D:\\NUMTASKS.TXT", 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
