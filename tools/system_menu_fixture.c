// SPDX-License-Identifier: GPL-2.0-or-later
// The states of a window's system menu follow the window
// (tools/test_system_menu_host.py).
//
// USER sets Restore, Move, Size, Minimize, Maximize and the default item of
// a system menu from the window each time the menu is about to be shown.
// The program opens the system menu of its window with SC_KEYMENU and a
// space in five states of the window, writes down what the menu shows when
// WM_INITMENUPOPUP arrives with the system flag, and ends the menu from
// WM_ENTERIDLE. Between two openings the states are those of the last one:
// GetSystemMenu alone does not set them.
//
// C:\SYSMENU.TXT gets one line per step, "step=RMSNX default", each letter
// 0 for an enabled item and 1 for a grayed one, in the order Restore, Move,
// Size, miNimize, maXimize.
#include <windows.h>

static char text[1024];
static int used, opened;
static const char *step;

static void record(HMENU menu, const char *name)
{
    static const UINT items[] = {SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE};
    int i;
    used += wsprintfA(text + used, "%s=", name);
    for (i = 0; i < 5; ++i)
        text[used++] = GetMenuState(menu, items[i], MF_BYCOMMAND) & MF_GRAYED ? '1' : '0';
    used += wsprintfA(text + used, " %x\r\n", GetMenuDefaultItem(menu, FALSE, GMDI_USEDISABLED));
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_INITMENUPOPUP && HIWORD(lp))
    {
        record((HMENU)wp, step);
        ++opened;
    }
    if (message == WM_ENTERIDLE && wp == MSGF_MENU)
        PostMessageA(window, WM_CANCELMODE, 0, 0);
    return DefWindowProcA(window, message, wp, lp);
}
// Opens the system menu and comes back when its menu loop has ended.
static void open(HWND window, const char *name)
{
    step = name;
    SendMessageA(window, WM_SYSCOMMAND, SC_KEYMENU, ' ');
}

void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    HWND window, plain;
    HANDLE file;
    DWORD written = 0;
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.hCursor = LoadCursorA(0, IDC_ARROW);
    c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    c.lpszClassName = "SystemMenu";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "SystemMenu", "System menu", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 200, 0, 0,
                             c.hInstance, 0);
    open(window, "normal");
    ShowWindow(window, SW_MAXIMIZE);
    record(GetSystemMenu(window, FALSE), "maximized, not opened");
    open(window, "maximized");
    ShowWindow(window, SW_SHOWMINIMIZED); // stays the active window, which SC_KEYMENU goes to
    open(window, "minimized");
    ShowWindow(window, SW_RESTORE); // maximized again, as it was before
    ShowWindow(window, SW_RESTORE);
    open(window, "restored");
    plain = CreateWindowExA(0, "SystemMenu", "No boxes", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 60, 60,
                            300, 200, 0, 0, c.hInstance, 0);
    open(plain, "no boxes");
    used += wsprintfA(text + used, "opened=%d\r\n", opened);
    file = CreateFileA("C:\\SYSMENU.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
