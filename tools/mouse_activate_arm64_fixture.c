/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WM_MOUSEACTIVATE at the windows of an ARM64 program. Three
   top-level windows: H and Q pass the message to DefWindowProc, C answers
   MA_NOACTIVATE. The same record as tools/mouse_activate_fixture.c: every
   question, the answer of DefWindowProc, every activation, every press and
   every change of the active, foreground and focus window, in order, into
   MOUSEA64.TXT beside the program; a left press on Q ends it. The message
   carries no pointers: a handle in wParam, two words in lParam.
   tools/test_mouse_activate_cases_native.py. */
#include <windows.h>

static HWND windows[3];
static const char letters[] = "HCQ";
static char text[3000], state[8], directory[MAX_PATH];

static char Letter(HWND window)
{
    for (int i = 0; i < 3; ++i)
        if (window && windows[i] == window) return letters[i];
    return window ? '?' : '-';
}
static void Line(const char *line)
{
    if (lstrlenA(text) + lstrlenA(line) + 3 < (int)sizeof text) wsprintfA(text + lstrlenA(text), "%s\r\n", line);
}
static void State(void)
{
    char now[8], line[40];
    wsprintfA(now, "%c%c%c", Letter(GetActiveWindow()), Letter(GetForegroundWindow()), Letter(GetFocus()));
    if (lstrcmpA(now, state))
    {
        lstrcpyA(state, now);
        wsprintfA(line, "STATE %s", now);
        Line(line);
    }
}
static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    const char who = Letter(window);
    char line[80];
    if (message == WM_MOUSEACTIVATE)
    {
        LRESULT answer;
        State();
        wsprintfA(line, "MA %c top=%c lp=%08x", who, Letter((HWND)wparam), (unsigned)lparam);
        Line(line);
        if (who == 'C') return MA_NOACTIVATE;
        answer = DefWindowProcA(window, message, wparam, lparam);
        wsprintfA(line, "MAR %c %u", who, (unsigned)answer);
        Line(line);
        return answer;
    }
    if (message == WM_ACTIVATE && LOWORD(wparam))
    {
        wsprintfA(line, "ACT %c wa=%u", who, (unsigned)LOWORD(wparam));
        Line(line);
    }
    if (message == WM_RBUTTONDOWN)
    {
        wsprintfA(line, "RDOWN %c", who);
        Line(line);
        return 0;
    }
    if (message == WM_RBUTTONUP || message == WM_CONTEXTMENU) return 0;
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
    {
        wsprintfA(line, "DOWN %c", who);
        Line(line);
        if (who == 'Q')
        {
            char path[MAX_PATH];
            DWORD written = 0;
            HANDLE file;
            State();
            wsprintfA(path, "%sMOUSEA64.TXT", directory);
            file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            WriteFile(file, text, (DWORD)lstrlenA(text), &written, NULL);
            CloseHandle(file);
            PostQuitMessage(0);
        }
        return 0;
    }
    if (message == WM_TIMER) State();
    return DefWindowProcA(window, message, wparam, lparam);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA klass = {0};
    MSG message;
    char *slash;
    (void)previous; (void)command; (void)show;
    GetModuleFileNameA(NULL, directory, sizeof directory);
    slash = directory + lstrlenA(directory);
    while (slash > directory && slash[-1] != '\\') --slash;
    *slash = 0;
    klass.lpfnWndProc = Procedure;
    klass.hInstance = instance;
    klass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    klass.lpszClassName = "Neo98MouseActivate64";
    RegisterClassA(&klass);
    for (int i = 0; i < 3; ++i)
    {
        char title[2] = {letters[i], 0};
        windows[i] = CreateWindowExA(0, klass.lpszClassName, title, WS_VISIBLE | WS_CAPTION | WS_SYSMENU, 10 + i * 130, 10,
                                     120, 100, NULL, NULL, instance, NULL);
    }
    SetTimer(windows[0], 1, 40, NULL);
    State();
    while (GetMessageA(&message, NULL, 0, 0) > 0) DispatchMessageA(&message);
    return 0;
}
