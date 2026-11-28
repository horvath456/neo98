/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * 0313h (the system menu the taskbar asks for) to a window that is not in
 * the foreground (tools/test_popup_menu_foreground_host.py), measured with
 * the same program in Windows 95 OSR2 and Windows 98 SE. USER's
 * DefWindowProc first makes a window of another queue than the foreground's
 * the foreground window, as SetForegroundWindow does, and opens nothing when
 * that is refused; then the menu at the point, or with lParam -1 beside the
 * window's taskbar button.
 *
 * POPFG.EXE starts itself as "POPFG.EXE window", a window "PopFgBack" that
 * notes what happens to it in C:\POPFGW.TXT (the menu loop and whether it
 * is the foreground window then) and ends a menu half a second after it
 * opened. The first process makes its own window "PopFgFront" the foreground
 * window and sends the other 0313h with SendMessageCallback, as the taskbar
 * does: once with a point, once with -1. After each it writes which window
 * is the foreground window. C:\POPFG.TXT (and COM1, when it opens) gets
 * name=value lines, those of the other process at the end.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static HWND back, front;
static int menus;

static void line(HANDLE file, const char *name, int value)
{
    char text[100];
    DWORD n;
    wsprintfA(text, "%s=%d\r\n", name, value);
    WriteFile(file, text, lstrlenA(text), &n, 0);
    if (file == out && com != INVALID_HANDLE_VALUE)
        WriteFile(com, text, lstrlenA(text), &n, 0);
}
static void pump(int milliseconds)
{
    MSG m;
    int i;
    for (i = 0; i < milliseconds / 50; ++i)
    {
        while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
            DispatchMessageA(&m);
        Sleep(50);
    }
}

/* The window in the background. */
static LRESULT CALLBACK back_procedure(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_ENTERMENULOOP)
    {
        char name[32];
        ++menus;
        wsprintfA(name, "menu%d.foreground", menus);
        line(out, name, GetForegroundWindow() == w);
        wsprintfA(name, "menu%d.active", menus);
        line(out, name, GetActiveWindow() == w);
        SetTimer(w, 1, 500, 0);
    }
    if (message == WM_TIMER && wp == 1)
    {
        KillTimer(w, 1);
        /* EndMenu is Windows 98's; WM_CANCELMODE to the owner ends a menu
           in both. */
        SendMessageA(w, WM_CANCELMODE, 0, 0);
        return 0;
    }
    if (message == WM_DESTROY)
    {
        line(out, "menus", menus);
        PostQuitMessage(0);
    }
    return DefWindowProcA(w, message, wp, lp);
}
static void window(void)
{
    static WNDCLASSA klass;
    MSG m;
    out = CreateFileA("C:\\POPFGW.TXT", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, CREATE_ALWAYS,
                      FILE_FLAG_WRITE_THROUGH, 0);
    klass.lpfnWndProc = back_procedure;
    klass.hInstance = GetModuleHandleA(0);
    klass.lpszClassName = "PopFgBack";
    klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    klass.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&klass);
    CreateWindowA("PopFgBack", "PopFgBack", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 320, 60, 260, 160, 0, 0,
                  klass.hInstance, 0);
    while (GetMessageA(&m, 0, 0, 0) > 0)
        DispatchMessageA(&m);
    CloseHandle(out);
    ExitProcess(0);
}

static void CALLBACK answered(HWND w, UINT message, ULONG_PTR data, LRESULT result)
{
    (void)w;
    (void)message;
    (void)result;
    line(out, data ? "keyboard.answered" : "point.answered", 1);
}

void mainCRTStartup(void)
{
    static WNDCLASSA klass;
    static STARTUPINFOA start;
    PROCESS_INFORMATION process;
    const char *command = GetCommandLineA(), *p;
    char path[MAX_PATH], text[64], copy[512];
    HANDLE file;
    DWORD n;
    int i;
    for (p = command; *p; ++p)
        if (p[0] == 'w' && p[1] == 'i' && p[2] == 'n' && p[3] == 'd' && p[4] == 'o' && p[5] == 'w')
            window();
    out = CreateFileA("C:\\POPFG.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    GetModuleFileNameA(0, path, sizeof path);
    lstrcpyA(text, "POPFG.EXE window");
    start.cb = sizeof start;
    line(out, "started", CreateProcessA(path, text, 0, 0, FALSE, 0, 0, 0, &start, &process) != 0);
    for (i = 0; i < 200 && !back; ++i)
    {
        Sleep(100);
        back = FindWindowA("PopFgBack", 0);
    }
    line(out, "window", back != 0);
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = GetModuleHandleA(0);
    klass.lpszClassName = "PopFgFront";
    klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    klass.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&klass);
    front = CreateWindowA("PopFgFront", "PopFgFront", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 60, 260, 160, 0, 0,
                          klass.hInstance, 0);
    pump(1500);
    SetForegroundWindow(front);
    pump(1000);
    line(out, "before.front", GetForegroundWindow() == front);
    if (back)
    {
        /* The point form, as a right click on the taskbar's button. */
        SendMessageCallbackA(back, 0x313, 0, MAKELPARAM(400, 120), answered, 0);
        pump(2500);
        line(out, "point.back", GetForegroundWindow() == back);
        line(out, "point.front", GetForegroundWindow() == front);
        SetForegroundWindow(front);
        pump(1000);
        line(out, "between.front", GetForegroundWindow() == front);
        /* The keyboard form. */
        SendMessageCallbackA(back, 0x313, 0, 0xffffffff, answered, 1);
        pump(2500);
        line(out, "keyboard.back", GetForegroundWindow() == back);
        line(out, "keyboard.front", GetForegroundWindow() == front);
        PostMessageA(back, WM_CLOSE, 0, 0);
        WaitForSingleObject(process.hProcess, 10000);
        file = CreateFileA("C:\\POPFGW.TXT", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        n = 0;
        if (file != INVALID_HANDLE_VALUE)
        {
            ReadFile(file, copy, sizeof copy - 1, &n, 0);
            CloseHandle(file);
        }
        WriteFile(out, copy, n, &n, 0);
        if (com != INVALID_HANDLE_VALUE)
            WriteFile(com, copy, n, &n, 0);
    }
    lstrcpyA(text, "done=1\r\n");
    WriteFile(out, text, lstrlenA(text), &n, 0);
    if (com != INVALID_HANDLE_VALUE)
    {
        WriteFile(com, text, lstrlenA(text), &n, 0);
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    }
    CloseHandle(out);
    ExitProcess(0);
}
