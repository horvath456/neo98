/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A machine saved after a process of it has ended.

   ENDED.EXE child    starts ENDED.EXE once more (the argument "gone"), the
                      process that starts last, and waits for its end; that
                      one makes a window and ends at once

   When the other process has ended, the window of the program
   takes the title "Ended ready", and tools/test_state_ended_host.py saves
   the machine. */
#include <windows.h>

static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_DESTROY && GetWindowLongA(hwnd, GWL_USERDATA)) PostQuitMessage(0);
    return DefWindowProcA(hwnd, message, wp, lp);
}

static const char *Argument(void)
{
    const char *line = GetCommandLineA();
    const char *space = line;
    if (*space == '"')
    {
        for (++space; *space && *space != '"'; ++space) {}
        if (*space) ++space;
    }
    else
        while (*space && *space != ' ') ++space;
    while (*space == ' ') ++space;
    return space;
}

void mainCRTStartup(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    WNDCLASSA window_class = {0};
    window_class.lpfnWndProc = Procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    window_class.lpszClassName = "Ended";
    RegisterClassA(&window_class);
    const char *argument = Argument();
    if (!lstrcmpA(argument, "gone"))
    {
        /* The process that started last ends first. */
        HWND gone = CreateWindowExA(0, "Ended", "Ended gone", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 260, 240, 120,
                                    0, 0, instance, 0);
        UpdateWindow(gone);
        ExitProcess(3);
    }
    HWND hwnd = CreateWindowExA(0, "Ended", "Ended waiting", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 360, 160, 0, 0,
                                instance, 0);
    SetWindowLongA(hwnd, GWL_USERDATA, 1);
    UpdateWindow(hwnd);
    HANDLE ended = 0;
    if (!lstrcmpA(argument, "child"))
    {
        char path[MAX_PATH], line[MAX_PATH + 16];
        GetModuleFileNameA(0, path, sizeof path);
        wsprintfA(line, "\"%s\" gone", path);
        STARTUPINFOA startup = {sizeof startup};
        PROCESS_INFORMATION process;
        if (CreateProcessA(0, line, 0, 0, FALSE, 0, 0, 0, &startup, &process))
        {
            CloseHandle(process.hThread);
            ended = process.hProcess;
        }
    }
    DWORD code = 0;
    if (ended && MsgWaitForMultipleObjects(1, &ended, FALSE, 20000, 0) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(ended, &code))
    {
        char title[64];
        wsprintfA(title, "Ended ready %lu", code);
        SetWindowTextA(hwnd, title);
    }
    if (ended) CloseHandle(ended);
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0) > 0) DispatchMessageA(&msg);
    ExitProcess(0);
}
