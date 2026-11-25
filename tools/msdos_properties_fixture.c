/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Opens the property sheet of C:\WINDOWS\COMMAND.COM the way Explorer's
   Properties does (ShellExecuteEx, verb "properties"): SHELL32 collects the
   MS-DOS program pages of PIFMGR through SHELL.DLL. The sheet runs on a
   thread of this process, which waits forty seconds for it.
   tools/test_display_advanced_host.py */
#include <windows.h>
#include <shellapi.h>
void mainCRTStartup(void)
{
    SHELLEXECUTEINFOA info = {0};
    MSG message;
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.lpVerb = "properties";
    info.lpFile = "C:\\WINDOWS\\COMMAND.COM";
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExA(&info);
    SetTimer(NULL, 1, 40000, NULL);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_TIMER && !message.hwnd) break;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess(0);
}
