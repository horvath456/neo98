// SPDX-License-Identifier: GPL-2.0-or-later
// The window is asked: WM_NCHITTEST before a press on a caption button, and
// the messages of SetWindowPos when the work area changes under a maximized
// window (tools/test_frame_ask_host.py).
//
// Two windows with the three boxes. The procedure of the second answers
// HTCAPTION wherever DefWindowProc finds a box. The program presses and
// releases the mouse over the minimize box of each (mouse_event): the first
// is minimized, the second is not and gets WM_NCLBUTTONDOWN with HTCAPTION.
// Then the first window is maximized and the work area loses 40 pixels at
// the bottom: the window is asked with WM_WINDOWPOSCHANGING, told with
// WM_WINDOWPOSCHANGED and WM_SIZE and ends 40 pixels higher up; with the
// whole screen as the work area again it is as before.
// C:\FRAMEASK.TXT gets one line "name=value" per finding.
#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#include <windows.h>

static char text[2048];
static int used;
static HWND plain, asked;
static int caption_presses, box_presses, changing, changed, sized, commands;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (window == asked)
    {
        if (message == WM_NCHITTEST)
        {
            const LRESULT hit = DefWindowProcA(window, message, wp, lp);
            return hit == HTREDUCE || hit == HTZOOM || hit == HTCLOSE ? HTCAPTION : hit;
        }
        if (message == WM_NCLBUTTONDOWN && wp == HTCAPTION) ++caption_presses;
        if (message == WM_NCLBUTTONDOWN && wp != HTCAPTION) ++box_presses;
        if (message == WM_SYSCOMMAND && (wp & 0xfff0) == SC_MINIMIZE) ++commands;
    }
    if (window == plain)
    {
        if (message == WM_WINDOWPOSCHANGING) ++changing;
        if (message == WM_WINDOWPOSCHANGED) ++changed;
        if (message == WM_SIZE) ++sized;
    }
    return DefWindowProcA(window, message, wp, lp);
}
static void pump(DWORD milliseconds)
{
    const DWORD until = GetTickCount() + milliseconds;
    MSG message;
    while (GetTickCount() < until)
    {
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(10);
    }
}
// A press and a release of the left button at a point of the 800 by 600
// screen, both in the queue before a message is taken: a press on a caption
// starts the loop that moves the window, which ends with the release.
static void click(int x, int y)
{
    const DWORD ax = x * 65536 / 800 + 1, ay = y * 65536 / 600 + 1;
    mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, ax, ay, 0, 0);
    pump(100);
    mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, ax, ay, 0, 0);
    pump(400);
}

void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    RECT r, work, lower, before, after, back;
    HANDLE file;
    DWORD written = 0;
    int x, y, first = 0, last = 0;
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.hCursor = LoadCursorA(0, IDC_ARROW);
    c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    c.lpszClassName = "FrameAsk";
    RegisterClassA(&c);
    plain = CreateWindowExA(0, "FrameAsk", "Plain", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 180, 0, 0,
                            c.hInstance, 0);
    asked = CreateWindowExA(0, "FrameAsk", "Asked", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 300, 300, 180, 0, 0,
                            c.hInstance, 0);
    pump(300);
    // Where the minimize box is: the points of the caption row DefWindowProc
    // answers HTREDUCE for. Both windows are of one size.
    GetWindowRect(plain, &r);
    y = r.top + GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION) / 2;
    for (x = r.left; x < r.right; ++x)
        if (DefWindowProcA(plain, WM_NCHITTEST, 0, MAKELPARAM(x, y)) == HTREDUCE)
        {
            if (!first) first = x;
            last = x;
        }
    line("minimize box found", first && last > first);
    x = (first + last) / 2 - r.left;
    y -= r.top;

    GetWindowRect(asked, &r);
    click(r.left + x, r.top + y);
    line("asked window minimized", IsIconic(asked) != 0);
    line("asked window got the press on its caption", caption_presses);
    line("asked window got a press on a box", box_presses);
    line("asked window got SC_MINIMIZE", commands);
    GetWindowRect(plain, &r);
    click(r.left + x, r.top + y);
    line("plain window minimized", IsIconic(plain) != 0);

    ShowWindow(plain, SW_RESTORE);
    ShowWindow(plain, SW_MAXIMIZE);
    pump(200);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    GetWindowRect(plain, &before);
    lower = work;
    lower.bottom -= 40;
    changing = changed = sized = 0;
    // A nonzero uiParam asks USER to place the windows anew, as the taskbar does.
    line("work area set", SystemParametersInfoA(SPI_SETWORKAREA, 1, &lower, 0));
    GetWindowRect(plain, &after);
    line("WM_WINDOWPOSCHANGING", changing);
    line("WM_WINDOWPOSCHANGED", changed);
    line("WM_SIZE", sized);
    line("bottom", after.bottom - before.bottom);
    line("other sides", (after.left - before.left) | (after.top - before.top) | (after.right - before.right));
    SystemParametersInfoA(SPI_SETWORKAREA, 1, &work, 0);
    GetWindowRect(plain, &back);
    line("back", EqualRect(&back, &before));
    file = CreateFileA("C:\\FRAMEASK.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
