/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Sets hooks for the whole session out of C:\SESSHOOK.DLL and stays: the
   small x86 program a shell built for another processor needs beside it to
   hook the x86 programs of its session. It writes what SetWindowsHookEx
   answered into C:\HOOKSET.TXT, starts Notepad, waits for its window, reads
   where it stands, removes the hooks and leaves. With "win16" the program it starts is the
   16-bit File Manager, which it closes again.
   tools/test_session_hooks_host.py, tools/test_session_hooks_win16_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U GetLastError(void);
API U LoadLibraryA(const char *);
API U WinExec(const char *, U);
API U GetProcAddress(U, const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U SetWindowsHookExA(int, U, U, U);
API int UnhookWindowsHookEx(U);
API U FindWindowA(const char *, const char *);
API int GetWindowRect(U, int *);
API int GetMessageA(U *, U, U, U);
API int DispatchMessageA(const U *);
API int PostMessageA(U, U, U, U);
API int IsWindow(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static void Report(const char *text)
{
    U written = 0, file = CreateFileA("C:\\HOOKSET.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
}
void mainCRTStartup(void)
{
    char text[480];
    const char *line = GetCommandLineA();
    int win16 = 0, closed = 0, rect[4] = {0, 0, 0, 0};
    U dll, cbt, messages, window = 0, refused, refused_error, removed;
    for (const char *p = line; *p; ++p)
    {
        if (p[0] == 'w' && p[1] == 'i' && p[2] == 'n' && p[3] == '1' && p[4] == '6') win16 = 1;
    }
    dll = LoadLibraryA("C:\\SESSHOOK.DLL");
    cbt = dll ? SetWindowsHookExA(5, GetProcAddress(dll, "_SessionCbt@12"), dll, 0) : 0;
    messages = dll ? SetWindowsHookExA(3, GetProcAddress(dll, "_SessionMessages@12"), dll, 0) : 0;
    /* A procedure outside a DLL cannot serve other processes. */
    refused = SetWindowsHookExA(5, (U)mainCRTStartup, 0, 0);
    refused_error = GetLastError();
    /* A program started after the hooks were set. */
    WinExec(win16 ? "C:\\WINDOWS\\WINFILE.EXE" : "C:\\WINDOWS\\NOTEPAD.EXE", 1);
    for (U i = 0; i < 600 && !window; ++i)
    {
        Sleep(100);
        window = FindWindowA(win16 ? "WFS_Frame" : "Notepad", 0);
    }
    if (window) GetWindowRect(window, rect);
    if (window && win16)
    {
        /* The 16-bit program runs on: let it show, then close it. It is not
           the only task, so it does not ask about the session. */
        Sleep(1500);
        PostMessageA(window, 0x10, 0, 0);
        for (U i = 0; i < 100 && IsWindow(window); ++i) Sleep(100);
        closed = !IsWindow(window);
    }
    removed = (cbt && UnhookWindowsHookEx(cbt)) + (messages && UnhookWindowsHookEx(messages));
    wsprintfA(text, "dll=%u\r\ncbt=%u\r\nmessages=%u\r\nrefused=%u\r\nrefused_error=%u\r\nwindow=%u\r\nrect=%d,%d,%d,%d\r\nremoved=%u\r\nclosed=%d\r\n",
              dll != 0, cbt != 0, messages != 0, refused, refused_error, window != 0, rect[0], rect[1], rect[2] - rect[0],
              rect[3] - rect[1], removed, closed);
    Report(text);
    ExitProcess(0);
}
