/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Win32 launcher for the Win16 version/dialog regression. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U GetVersion(void);
API U WinExec(const char *, U);
API U FindWindowA(const char *, const char *);
API U IsWindowVisible(U);
API U SendMessageA(U, U, U, U);

static void child(const char *path, const char *caption, U error)
{
    if (WinExec(path, 0) < 32) ExitProcess(error);
    for (U i = 0; i < 200; ++i) {
        U window = FindWindowA("#32770", caption);
        if (window) {
            if (!IsWindowVisible(window)) ExitProcess(error + 1);
            SendMessageA(window, 0x111, 2, 0);
            return;
        }
        if (FindWindowA("#32770", "FAIL version")) ExitProcess(error + 2);
        Sleep(10);
    }
    ExitProcess(error + 3);
}

void mainCRTStartup(void)
{
    if ((GetVersion() & 0xffff) != 0x0a04) ExitProcess(56101);
    child("C:\\VER95.EXE", "Version95", 56110);
    child("C:\\VER31.EXE", "Version31", 56120);
    ExitProcess(0);
}
