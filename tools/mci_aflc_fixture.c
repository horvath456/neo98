/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the game's MCI_OPEN flags, a string open, and recovery after failure.
   Run unchanged on Win98 and NEO98, with C:\TITUS.AVI. */
#include <windows.h>
#include <mmsystem.h>
#include <digitalv.h>

void mainCRTStartup(void)
{
    MCI_DGV_OPEN_PARMSA p;
    MCIERROR command, string, empty, close = 0;
    char codec[128], report[512];
    DWORD count;
    HANDLE file;
    HWND parent = CreateWindowExA(0, "STATIC", "AFLC probe", WS_OVERLAPPEDWINDOW,
                                 0, 0, 320, 200, 0, 0, GetModuleHandleA(0), 0);
    DWORD *words = (DWORD *)&p;
    unsigned i;
    for (i = 0; i < sizeof p / sizeof *words; ++i) words[i] = 0;
    p.lpstrDeviceType = "avivideo";
    p.lpstrElementName = "C:\\TITUS.AVI";
    p.dwStyle = WS_CHILD | WS_VISIBLE;
    p.hWndParent = parent;
    GetPrivateProfileStringA("drivers32", "vidc.aflc", "<absent>", codec, sizeof codec, "system.ini");
    command = mciSendCommandA(0, MCI_OPEN, 0x32200, (DWORD_PTR)&p);
    if (!command) mciSendCommandA(p.wDeviceID, MCI_CLOSE, 0, 0);
    string = mciSendStringA("open C:\\TITUS.AVI type avivideo alias probe", 0, 0, 0);
    if (!string) mciSendStringA("close probe", 0, 0, 0);
    empty = mciSendStringA("open avivideo alias recover", 0, 0, 0);
    if (!empty) close = mciSendStringA("close recover", 0, 0, 0);
    count = wsprintfA(report, "vidc.aflc=%s\r\ncommand=%lu\r\nstring=%lu\r\nempty=%lu\r\nclose=%lu\r\n",
                      codec, command, string, empty, close);
    file = CreateFileA("C:\\AFLC.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, report, count, &count, 0);
    CloseHandle(file);
    DestroyWindow(parent);
    ExitProcess(command == 6 && string == 6 && !empty && !close ? 0 : 6230);
}
