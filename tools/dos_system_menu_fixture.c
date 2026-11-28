/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * The system menu of the MS-DOS window (tools/test_dos_system_menu_host.py),
 * measured with the same program in Windows 95 OSR2 and Windows 98 SE.
 * WINOLDAP adds its own items to it. The program opens C:\WINDOWS\COMMAND.COM,
 * waits for its window (class "tty"), and writes:
 *
 * - every item of the window's system menu (GetSystemMenu): position, id,
 *   state and text, and those of a popup under it;
 * - the system commands of the items (found by their texts), sent as
 *   WM_SYSCOMMAND: Toolbar twice (the client area's height and the item's
 *   check after each); Mark (the window's title) and Escape; Mark again
 *   with a block dragged over two lines and Enter, which copies it (the
 *   titles and the text on the clipboard); Scroll and Escape; Properties
 *   (the title of the window that opens, then closed again); Alt+Space
 *   posted as WM_SYSKEYDOWN (whether a menu window shows); and Paste of
 *   "exit" and a carriage return from the clipboard, which ends COMMAND.COM
 *   (whether the window closed, else its title).
 *
 * C:\DOSMENU.TXT (and COM1, when it opens) gets name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static char line[512];
static int length;

static void add(const char *text)
{
    while (*text && length < (int)sizeof line - 3)
        line[length++] = *text++;
}
static void addf(const char *format, int value)
{
    char text[64];
    wsprintfA(text, format, value);
    add(text);
}
static void flush(void)
{
    DWORD n;
    add("\r\n");
    WriteFile(out, line, length, &n, 0);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, line, length, &n, 0);
    length = 0;
}
static void begin(const char *name)
{
    length = 0;
    add(name);
    add("=");
}
static void wait(int milliseconds)
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

static void items(HMENU menu, const char *prefix)
{
    int count = GetMenuItemCount(menu), i;
    char name[32], text[64];
    begin(prefix);
    length--;
    add(".count=");
    addf("%d", count);
    flush();
    for (i = 0; i < count; ++i)
    {
        const UINT id = GetMenuItemID(menu, i), state = GetMenuState(menu, i, MF_BYPOSITION);
        const int n = GetMenuStringA(menu, i, text, sizeof text, MF_BYPOSITION);
        wsprintfA(name, "%s.%d", prefix, i);
        begin(name);
        addf("id=%x", (int)id);
        addf(",state=%x", (int)(state & 0xff));
        add(",text=\"");
        if (n > 0)
        {
            int k;
            for (k = 0; k < n; ++k)
            {
                char one[2] = {text[k] == '\t' ? '|' : text[k], 0};
                add(one);
            }
        }
        add("\"");
        flush();
        if (id == 0xffffffffu && GetSubMenu(menu, i))
        {
            wsprintfA(name, "%s.%d", prefix, i);
            items(GetSubMenu(menu, i), name);
        }
    }
}

/* The command of the item with this text, also in a popup; else the id
   the strings of WINOLDAP give it. */
static UINT command(HMENU menu, const char *wanted, UINT otherwise)
{
    int count = GetMenuItemCount(menu), i;
    char text[64];
    for (i = 0; i < count; ++i)
    {
        const UINT id = GetMenuItemID(menu, i);
        if (id == 0xffffffffu && GetSubMenu(menu, i))
        {
            const UINT inner = command(GetSubMenu(menu, i), wanted, 0);
            if (inner)
                return inner;
        }
        else if (GetMenuStringA(menu, i, text, sizeof text, MF_BYPOSITION) > 0)
        {
            int k = 0;
            while (wanted[k] && text[k] == wanted[k])
                ++k;
            if (!wanted[k])
                return id;
        }
    }
    return otherwise;
}

static int client_height(HWND w)
{
    RECT r;
    GetClientRect(w, &r);
    return r.bottom;
}

static void title(const char *name, HWND w)
{
    char text[128];
    const int n = GetWindowTextA(w, text, sizeof text);
    begin(name);
    add("\"");
    if (n > 0)
        add(text);
    add("\"");
    flush();
}

static HWND sheet;
static BOOL CALLBACK find_sheet(HWND w, LPARAM tty)
{
    char klass[32];
    GetClassNameA(w, klass, sizeof klass);
    if (!lstrcmpA(klass, "#32770") && IsWindowVisible(w) && (HWND)tty != w)
    {
        sheet = w;
        return FALSE;
    }
    return TRUE;
}

void mainCRTStartup(void)
{
    HWND tty = 0;
    HMENU menu;
    DWORD n;
    HGLOBAL memory;
    int i, before;
    UINT toolbar, properties, mark, scroll, paste;
    out = CreateFileA("C:\\DOSMENU.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    begin("started");
    addf("%d", WinExec("C:\\WINDOWS\\COMMAND.COM", SW_SHOWNORMAL) > 31);
    flush();
    for (i = 0; i < 300 && !tty; ++i)
    {
        wait(100);
        tty = FindWindowA("tty", 0);
    }
    begin("window");
    addf("%d", tty != 0);
    flush();
    if (tty)
    {
        wait(3000);
        menu = GetSystemMenu(tty, FALSE);
        items(menu, "menu");
        toolbar = command(menu, "&Toolbar", 0xe00e);
        properties = command(menu, "&Properties", 0xe005);
        mark = command(menu, "Mar&k", 0xe002);
        scroll = command(menu, "Scro&ll", 0xe004);
        paste = command(menu, "&Paste", 0xe001);
        /* Toolbar, twice. */
        before = client_height(tty);
        SendMessageA(tty, WM_SYSCOMMAND, toolbar, 0);
        wait(1000);
        begin("toolbar1");
        addf("%d", client_height(tty) - before);
        addf(",state=%x", (int)(GetMenuState(GetSystemMenu(tty, FALSE), toolbar, MF_BYCOMMAND) & 0xff));
        flush();
        SendMessageA(tty, WM_SYSCOMMAND, toolbar, 0);
        wait(1000);
        begin("toolbar2");
        addf("%d", client_height(tty) - before);
        addf(",state=%x", (int)(GetMenuState(GetSystemMenu(tty, FALSE), toolbar, MF_BYCOMMAND) & 0xff));
        flush();
        /* Mark, and Escape. */
        title("title", tty);
        PostMessageA(tty, WM_SYSCOMMAND, mark, 0);
        wait(1000);
        title("mark", tty);
        PostMessageA(tty, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
        PostMessageA(tty, WM_KEYUP, VK_ESCAPE, 0xc0010001);
        wait(1000);
        title("unmarked", tty);
        /* Mark, a block dragged over the first two lines below the
           toolbar, and Enter: Copy. */
        if (OpenClipboard(0))
        {
            EmptyClipboard();
            CloseClipboard();
        }
        PostMessageA(tty, WM_SYSCOMMAND, mark, 0);
        wait(1000);
        PostMessageA(tty, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 31));
        wait(300);
        PostMessageA(tty, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, 31 + 2 * 12 + 4));
        wait(300);
        PostMessageA(tty, WM_LBUTTONUP, 0, MAKELPARAM(200, 31 + 2 * 12 + 4));
        wait(1000);
        title("selected", tty);
        PostMessageA(tty, WM_KEYDOWN, VK_RETURN, 0x001c0001);
        PostMessageA(tty, WM_KEYUP, VK_RETURN, 0xc01c0001);
        wait(1000);
        title("copied", tty);
        begin("clipboard");
        add("\"");
        if (OpenClipboard(0))
        {
            HANDLE text = GetClipboardData(CF_TEXT);
            const char *p = text ? (const char *)GlobalLock(text) : 0;
            if (p)
            {
                int k;
                for (k = 0; p[k] && k < 60; ++k)
                {
                    char one[2] = {p[k] == '\r' ? '|' : p[k] == '\n' ? '/' : p[k], 0};
                    add(one);
                }
                GlobalUnlock(text);
            }
            CloseClipboard();
        }
        add("\"");
        flush();
        /* Scroll, and Escape. */
        PostMessageA(tty, WM_SYSCOMMAND, scroll, 0);
        wait(1000);
        title("scroll", tty);
        PostMessageA(tty, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
        PostMessageA(tty, WM_KEYUP, VK_ESCAPE, 0xc0010001);
        wait(1000);
        title("unscrolled", tty);
        /* Properties. */
        PostMessageA(tty, WM_SYSCOMMAND, properties, 0);
        for (i = 0; i < 100 && !sheet; ++i)
        {
            wait(100);
            EnumWindows(find_sheet, (LPARAM)tty);
        }
        title("properties", sheet);
        if (sheet)
        {
            PostMessageA(sheet, WM_COMMAND, IDCANCEL, 0);
            wait(1500);
            begin("properties.closed");
            addf("%d", !IsWindow(sheet) || !IsWindowVisible(sheet));
            flush();
        }
        /* Alt+Space: the system menu opens (a menu window shows); then
           WM_CANCELMODE to the window ends it. */
        SetForegroundWindow(tty);
        wait(500);
        PostMessageA(tty, WM_SYSKEYDOWN, VK_SPACE, 0x20390001);
        PostMessageA(tty, WM_SYSKEYUP, VK_SPACE, 0xe0390001);
        wait(1500);
        {
            HWND popup = FindWindowA("#32768", 0);
            begin("altspace.menu");
            addf("%d", popup != 0 && IsWindowVisible(popup));
            flush();
        }
        PostMessageA(tty, WM_CANCELMODE, 0, 0);
        wait(1000);
        title("altspace.after", tty);
        /* Paste "exit" and Enter. */
        if (OpenClipboard(0))
        {
            EmptyClipboard();
            memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 8);
            lstrcpyA((char *)GlobalLock(memory), "exit\r\n");
            GlobalUnlock(memory);
            SetClipboardData(CF_TEXT, memory);
            CloseClipboard();
        }
        PostMessageA(tty, WM_SYSCOMMAND, paste, 0);
        for (i = 0; i < 100 && IsWindow(tty) && IsWindowVisible(tty); ++i)
            wait(100);
        begin("pasted.closed");
        addf("%d", !IsWindow(tty) || !IsWindowVisible(tty));
        flush();
        if (IsWindow(tty))
            title("pasted", tty);
    }
    lstrcpyA(line, "done=1");
    length = lstrlenA(line);
    flush();
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    CloseHandle(out);
    ExitProcess(0);
}
