// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U ExitWindowsEx(U, U);
API void ExitProcess(U);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API void Sleep(U);
API char *GetCommandLineA(void);
void mainCRTStartup(void)
{
    U startup[17] = {68}, process[4] = {0};
    U veto = 0;
    const char *command = GetCommandLineA();
    for (U i = 0; command[i]; ++i) if (command[i] == '-') { veto = 1; break; }
    char wait[] = "WAIT98.EXE --veto";
    if (!CreateProcessA("C:\\WAIT98.EXE", veto ? wait : 0, 0, 0, 0, 0, 0, 0, startup, process)) ExitProcess(2);
    Sleep(1000);
    ExitProcess(!ExitWindowsEx(1, 0));
}
