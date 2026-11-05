/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CreateProcess with CREATE_SUSPENDED and a desktop name, the way MSI starts
   the EXE of a custom action: the program is copied to a.TMP file,
   started suspended with lpDesktop "WinSta0\Default", and runs only after
   ResumeThread on the thread handle. The same program is the child. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateFileA(const char *, U, U, U, U, U, U);
API U CloseHandle(U);
API U CopyFileA(const char *, const char *, U);
API U GetFileAttributesA(const char *);
API U DeleteFileA(const char *);
API U GetModuleFileNameA(U, char *, U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API void Sleep(U);
API U SuspendThread(U);
API U ResumeThread(U);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API void ExitProcess(U);
static const char copy[] = "C:\\WINDOWS\\TEMP\\ARMSUSP.TMP", marker[] = "C:\\ARMSUSP.TXT";
static int child(const char *line)
{
    for (; *line; ++line)
        if (line[0] == ' ' && line[1] == '/' && line[2] == 'c') return 1;
    return 0;
}
void mainCRTStartup(void)
{
    if (child(GetCommandLineA()))
    {
        U h = CreateFileA(marker, 0x40000000, 0, 0, 2, 0x80, 0);
        if (h == ~0u) ExitProcess(20);
        CloseHandle(h);
        ExitProcess(7);
    }
    char self[260];
    if (!GetModuleFileNameA(0, self, sizeof self)) ExitProcess(1);
    DeleteFileA(marker);
    DeleteFileA(copy);
    if (!CopyFileA(self, copy, 0)) ExitProcess(2);
    char line[] = "\"C:\\WINDOWS\\TEMP\\ARMSUSP.TMP\" /c";
    U startup[17] = {68};
    startup[2] = (U)"WinSta0\\Default";
    U info[4] = {0};
    /* NORMAL_PRIORITY_CLASS | CREATE_SUSPENDED, no current directory. */
    if (!CreateProcessA(0, line, 0, 0, 0, 0x24, 0, 0, startup, info)) ExitProcess(3);
    Sleep(300);
    if (GetFileAttributesA(marker) != ~0u) ExitProcess(4); /* ran before ResumeThread */
    if (SuspendThread(info[1]) != 1) ExitProcess(5);
    if (ResumeThread(info[1]) != 2) ExitProcess(6);
    Sleep(100);
    if (GetFileAttributesA(marker) != ~0u) ExitProcess(8);
    if (ResumeThread(info[1]) != 1) ExitProcess(9);
    if (WaitForSingleObject(info[0], 10000) != 0) ExitProcess(10);
    U code = 0;
    if (!GetExitCodeProcess(info[0], &code) || code != 7) ExitProcess(11);
    if (GetFileAttributesA(marker) == ~0u) ExitProcess(12);
    if (ResumeThread(info[1]) != ~0u) ExitProcess(13); /* an ended thread */
    CloseHandle(info[1]);
    CloseHandle(info[0]);
    DeleteFileA(marker);
    DeleteFileA(copy);
    ExitProcess(0);
}
