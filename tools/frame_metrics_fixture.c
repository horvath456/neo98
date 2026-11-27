// SPDX-License-Identifier: GPL-2.0-or-later
// SPI_SETNONCLIENTMETRICS gives the windows that are open their new frame
// (tools/test_frame_metrics_host.py).
//
// Three windows are open: a main window with a sizing border, a caption and
// a menu bar, a child of it with a sizing border and a caption, and a popup
// with a caption and a fixed frame. The program makes the sizing border 3
// units wider, the caption 10 pixels and the menu bar 6 pixels higher and
// writes down, for each window, how its rectangle and its client area changed
// and which messages it got meanwhile; then it sets the old sizes again.
//
//   FRAMES.EXE          the three windows, and the box of the start of a drag
//   FRAMES.EXE minset   SPI_SETMINIMIZEDMETRICS with SPIF_UPDATEINIFILE: a
//                       width of 200, gaps of 7 and 9, arranged from the
//                       top right downwards
//   FRAMES.EXE minget   MINIMIZEDMETRICS as a session starts with it
// C:\FRAMES.TXT gets one line "name=value" per finding.
#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#include <windows.h>

static char text[4096];
static int used;
static HWND windows[3];
static struct { int changing, calculate, changed, framed, size, icon, setting; } seen[3];

static void line(const char *name, const char *window, long value)
{
    used += wsprintfA(text + used, "%s%s%s=%ld\r\n", window, *window ? " " : "", name, value);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    int i;
    for (i = 0; i < 3; ++i)
        if (windows[i] == window)
        {
            if (message == WM_WINDOWPOSCHANGING) ++seen[i].changing;
            if (message == WM_NCCALCSIZE) ++seen[i].calculate;
            if (message == WM_WINDOWPOSCHANGED)
            {
                ++seen[i].changed;
                if (((WINDOWPOS *)lp)->flags & SWP_FRAMECHANGED) ++seen[i].framed;
            }
            if (message == WM_SIZE) ++seen[i].size;
            if (message == WM_SETICON && wp == 2) ++seen[i].icon;
            if (message == WM_SETTINGCHANGE) ++seen[i].setting;
        }
    return DefWindowProcA(window, message, wp, lp);
}
static void rectangles(RECT *outer, RECT *client)
{
    int i;
    for (i = 0; i < 3; ++i)
    {
        GetWindowRect(windows[i], outer + i);
        GetClientRect(windows[i], client + i);
    }
    // The child in the client area of its parent, as SetWindowPos places it.
    MapWindowPoints(0, windows[0], (POINT *)(outer + 1), 2);
}

static int ends(const char *command, const char *word)
{
    int n = lstrlenA(command), m = lstrlenA(word);
    return n > m && !lstrcmpA(command + n - m, word) && command[n - m - 1] == ' ';
}
static void finish(void)
{
    DWORD written = 0;
    HANDLE file = CreateFileA("C:\\FRAMES.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
static void minimized(int set)
{
    MINIMIZEDMETRICS m;
    m.cbSize = sizeof m;
    if (set)
    {
        m.iWidth = 200;
        m.iHorzGap = 7;
        m.iVertGap = 9;
        m.iArrange = ARW_TOPRIGHT | ARW_DOWN;
        line("set", "", SystemParametersInfoA(SPI_SETMINIMIZEDMETRICS, sizeof m, &m, SPIF_UPDATEINIFILE));
    }
    m.iWidth = m.iHorzGap = m.iVertGap = m.iArrange = -1;
    line("get", "", SystemParametersInfoA(SPI_GETMINIMIZEDMETRICS, sizeof m, &m, 0));
    line("width", "", m.iWidth);
    line("horizontal gap", "", m.iHorzGap);
    line("vertical gap", "", m.iVertGap);
    line("arrange", "", m.iArrange & ~ARW_HIDE);
    line("minimized window", "", GetSystemMetrics(SM_CXMINIMIZED) - 2 * GetSystemMetrics(SM_CXFIXEDFRAME));
    line("cell wider", "", GetSystemMetrics(SM_CXMINSPACING) - GetSystemMetrics(SM_CXMINIMIZED));
    line("cell higher", "", GetSystemMetrics(SM_CYMINSPACING) - GetSystemMetrics(SM_CYMINIMIZED));
    finish();
}

void mainCRTStartup(void)
{
    static const char *const names[3] = {"main", "child", "popup"};
    WNDCLASSA c = {0};
    NONCLIENTMETRICSA old, wide;
    RECT before[3], client_before[3], after[3], client_after[3], back[3], client_back[3];
    HMENU menu;
    BOOL answer;
    int i, caption, bar, frame, frame_y;
    if (ends(GetCommandLineA(), "minset")) minimized(1);
    if (ends(GetCommandLineA(), "minget")) minimized(0);
    line("drag box", "", GetSystemMetrics(SM_CXDRAG) * 100 + GetSystemMetrics(SM_CYDRAG));
    menu = CreateMenu();
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.hCursor = LoadCursorA(0, IDC_ARROW);
    c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    c.lpszClassName = "FrameMetrics";
    RegisterClassA(&c);
    AppendMenuA(menu, MF_STRING, 1, "&File");
    windows[0] = CreateWindowExA(0, "FrameMetrics", "Main", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 60, 60,
                                 420, 320, 0, menu, c.hInstance, 0);
    windows[1] = CreateWindowExA(0, "FrameMetrics", "Child", WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_VISIBLE, 20, 20,
                                 220, 140, windows[0], 0, c.hInstance, 0);
    windows[2] = CreateWindowExA(0, "FrameMetrics", "Popup", WS_POPUP | WS_CAPTION | WS_VISIBLE, 520, 60, 200, 120, 0, 0,
                                 c.hInstance, 0);
    old.cbSize = sizeof old;
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof old, &old, 0);
    caption = GetSystemMetrics(SM_CYCAPTION);
    bar = GetSystemMetrics(SM_CYMENU);
    frame = GetSystemMetrics(SM_CXFRAME);
    frame_y = GetSystemMetrics(SM_CYFRAME);
    rectangles(before, client_before);

    wide.cbSize = sizeof wide; // no copy of the structure: the program has no C library
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof wide, &wide, 0);
    wide.iBorderWidth += 3;
    wide.iCaptionHeight += 10;
    wide.iMenuHeight += 6;
    answer = SystemParametersInfoA(SPI_SETNONCLIENTMETRICS, sizeof wide, &wide, 0);
    line("set", "", answer);
    line("caption", "", GetSystemMetrics(SM_CYCAPTION) - caption);
    line("menu bar", "", GetSystemMetrics(SM_CYMENU) - bar);
    line("sizing border", "", GetSystemMetrics(SM_CXFRAME) - frame);
    line("sizing border down", "", GetSystemMetrics(SM_CYFRAME) - frame_y);
    rectangles(after, client_after);
    for (i = 0; i < 3; ++i)
    {
        line("left", names[i], after[i].left - before[i].left);
        line("top", names[i], after[i].top - before[i].top);
        line("right", names[i], after[i].right - before[i].right);
        line("bottom", names[i], after[i].bottom - before[i].bottom);
        line("client wider", names[i], client_after[i].right - client_before[i].right);
        line("client higher", names[i], client_after[i].bottom - client_before[i].bottom);
        line("WM_WINDOWPOSCHANGING", names[i], seen[i].changing);
        line("WM_NCCALCSIZE", names[i], seen[i].calculate);
        line("WM_WINDOWPOSCHANGED", names[i], seen[i].changed);
        line("SWP_FRAMECHANGED", names[i], seen[i].framed);
        line("WM_SIZE", names[i], seen[i].size);
        line("WM_SETICON 2", names[i], seen[i].icon);
        line("WM_SETTINGCHANGE", names[i], seen[i].setting);
    }
    SystemParametersInfoA(SPI_SETNONCLIENTMETRICS, sizeof old, &old, 0);
    rectangles(back, client_back);
    for (i = 0; i < 3; ++i)
        line("back", names[i], EqualRect(back + i, before + i) && EqualRect(client_back + i, client_before + i));
    finish();
}
