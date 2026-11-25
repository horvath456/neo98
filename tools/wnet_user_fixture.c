/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WNetGetUserA of the user Windows logged on at its start. Writes
   C:\WNETUSER.TXT: the answer and the name, "0 NEO98" for setup's user.
   Then it opens Passwords of the Control Panel, as "control password.cpl"
   from Start > Run would. tools/test_logon_start_host.py */
#include <windows.h>
#include <winnetwk.h>

void mainCRTStartup(void)
{
    char name[64] = {0}, line[160];
    DWORD size = sizeof name, written = 0;
    DWORD result = WNetGetUserA(NULL, name, &size);
    wsprintfA(line, "%lu %s\r\n", result, name);
    HANDLE file = CreateFileA("C:\\WNETUSER.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, line, lstrlenA(line), &written, NULL);
    CloseHandle(file);
    WinExec("control.exe password.cpl", SW_SHOWNORMAL);
    ExitProcess(0);
}
