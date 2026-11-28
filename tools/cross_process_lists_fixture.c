/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * The messages of list boxes, combo boxes and edit controls that carry a
 * pointer, sent to the controls of another process
 * (tools/test_cross_process_lists_host.py), measured with the same program
 * in Windows 95 OSR2 and Windows 98 SE.
 *
 * XPROC.EXE starts itself as "XPROC.EXE server", which opens a window
 * "XProcServer" with a drop-down combo box (101), a list box (102), a
 * multi-line edit (103), an owner-drawn combo box (104) and list box (105)
 * without CBS_HASSTRINGS and LBS_HASSTRINGS, and a list box with
 * LBS_EXTENDEDSEL (106), and answers WM_GETMINMAXINFO by setting its
 * minimum tracking size to 123 x 45. The first process sends the messages
 * and writes what comes back: answers, the buffers they fill (bytes outside
 * 20h..7Eh as \xx) and the rectangles relative to the control. When
 * C:\XP16.EXE exists it starts it with WinExec (a 16-bit sender of the same
 * kind, tools/cross_process_lists_win16_fixture.asm) and waits for
 * C:\XP16OUT.TXT before it closes the server. With "getline" on its command
 * line it sends EM_GETLINE too, at the end: in Windows 95 and 98 that ends
 * the sender with a fault. C:\XPROC.TXT (and COM1, when it opens) gets
 * name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static char line[1024];
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
static void quoted(const char *text, int count)
{
    int i;
    add("\"");
    for (i = 0; i < count; ++i)
    {
        const unsigned char c = (unsigned char)text[i];
        if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"')
        {
            char one[2] = {(char)c, 0};
            add(one);
        }
        else
            addf("\\%02x", c);
    }
    add("\"");
}
static void flush(void)
{
    DWORD n;
    add("\r\n");
    WriteFile(out, line, length, &n, 0);
    if (com != INVALID_HANDLE_VALUE)
    {
        WriteFile(com, line, length, &n, 0);
        FlushFileBuffers(com);
    }
    length = 0;
}
static void put(const char *name, int value)
{
    length = 0;
    add(name);
    addf("=%d", value);
    flush();
}
/* name=answer,"buffer" */
static void put_text(const char *name, int value, const char *text, int count)
{
    /* An answer that is no length shows the whole buffer. */
    if (count < 0 || count > 64)
        count = 64;
    length = 0;
    add(name);
    addf("=%d,", value);
    quoted(text, count);
    flush();
}
static void put_rect(const char *name, int value, const RECT *r, HWND relative)
{
    RECT w = {0, 0, 0, 0};
    if (relative)
        GetWindowRect(relative, &w);
    length = 0;
    add(name);
    addf("=%d", value);
    addf(",%d", r->left - w.left);
    addf(",%d", r->top - w.top);
    addf(",%d", r->right - w.left);
    addf(",%d", r->bottom - w.top);
    flush();
}
static void fill(char *buffer, int count, char c)
{
    int i;
    for (i = 0; i < count; ++i)
        buffer[i] = c;
}

/* The server. */
static LRESULT CALLBACK server_procedure(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_GETMINMAXINFO)
    {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = 123;
        info->ptMinTrackSize.y = 45;
        return 0;
    }
    if (message == WM_MEASUREITEM)
    {
        ((MEASUREITEMSTRUCT *)lp)->itemHeight = 14;
        return TRUE;
    }
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(w, message, wp, lp);
}
static void serve(void)
{
    static WNDCLASSA klass;
    HWND window;
    HINSTANCE instance = GetModuleHandleA(0);
    MSG m;
    klass.lpfnWndProc = server_procedure;
    klass.hInstance = instance;
    klass.lpszClassName = "XProcServer";
    klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&klass);
    window = CreateWindowA("XProcServer", "XProcServer", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 10, 330, 260, 0, 0,
                           instance, 0);
    CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 10, 10, 150, 120, window,
                  (HMENU)101, instance, 0);
    CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 10, 40, 150, 80, window,
                  (HMENU)102, instance, 0);
    CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 170, 10, 150, 80,
                  window, (HMENU)103, instance, 0);
    CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | WS_VSCROLL, 10, 130,
                  150, 120, window, (HMENU)104, instance, 0);
    CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_OWNERDRAWFIXED, 170, 100, 150, 60, window,
                  (HMENU)105, instance, 0);
    CreateWindowA("LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_EXTENDEDSEL, 10, 160, 150, 60, window,
                  (HMENU)106, instance, 0);
    while (GetMessageA(&m, 0, 0, 0) > 0)
    {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    ExitProcess(0);
}

static void combo(HWND c)
{
    char buffer[64];
    DWORD start = 0xaaaa, end = 0xbbbb;
    RECT r;
    int n;
    put("c.add0", (int)SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Alpha"));
    put("c.add1", (int)SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Beta"));
    put("c.add2", (int)SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)"Gamma"));
    put("c.insert", (int)SendMessageA(c, CB_INSERTSTRING, 1, (LPARAM)"Inserted"));
    put("c.count", (int)SendMessageA(c, CB_GETCOUNT, 0, 0));
    put("c.find", (int)SendMessageA(c, CB_FINDSTRING, (WPARAM)-1, (LPARAM)"Gam"));
    put("c.findexact", (int)SendMessageA(c, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"beta"));
    put("c.select", (int)SendMessageA(c, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)"Ins"));
    put("c.cursel", (int)SendMessageA(c, CB_GETCURSEL, 0, 0));
    put("c.textlen", (int)SendMessageA(c, CB_GETLBTEXTLEN, 2, 0));
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(c, CB_GETLBTEXT, 2, (LPARAM)buffer);
    put_text("c.text", n, buffer, 8);
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(c, WM_GETTEXT, 16, (LPARAM)buffer);
    put_text("c.gettext", n, buffer, 20);
    put("c.seteditsel", (int)SendMessageA(c, CB_SETEDITSEL, 0, MAKELPARAM(1, 4)));
    n = (int)SendMessageA(c, CB_GETEDITSEL, (WPARAM)&start, (LPARAM)&end);
    length = 0;
    add("c.geteditsel");
    addf("=%x", n);
    addf(",%x", (int)start);
    addf(",%x", (int)end);
    flush();
    fill((char *)&r, sizeof r, 0x77);
    n = (int)SendMessageA(c, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&r);
    put_rect("c.droppedrect", n, &r, c);
    put("c.dir", (int)SendMessageA(c, CB_DIR, DDL_READWRITE, (LPARAM)"C:\\XPDIR\\*.TXT"));
    put("c.dircount", (int)SendMessageA(c, CB_GETCOUNT, 0, 0));
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(c, CB_GETLBTEXT, 4, (LPARAM)buffer);
    put_text("c.dirtext", n, buffer, n > 0 ? n + 1 : 4);
}

static void list(HWND l)
{
    char buffer[64];
    static const int stops[] = {10, 20};
    RECT r;
    int n, i;
    put("l.add0", (int)SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"One"));
    put("l.add1", (int)SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"Two"));
    put("l.add2", (int)SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"Three"));
    put("l.insert", (int)SendMessageA(l, LB_INSERTSTRING, 0, (LPARAM)"Zero"));
    put("l.count", (int)SendMessageA(l, LB_GETCOUNT, 0, 0));
    put("l.find", (int)SendMessageA(l, LB_FINDSTRING, (WPARAM)-1, (LPARAM)"Th"));
    put("l.findexact", (int)SendMessageA(l, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)"two"));
    put("l.select", (int)SendMessageA(l, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)"On"));
    put("l.cursel", (int)SendMessageA(l, LB_GETCURSEL, 0, 0));
    put("l.textlen", (int)SendMessageA(l, LB_GETTEXTLEN, 3, 0));
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(l, LB_GETTEXT, 3, (LPARAM)buffer);
    put_text("l.text", n, buffer, 8);
    fill((char *)&r, sizeof r, 0x77);
    n = (int)SendMessageA(l, LB_GETITEMRECT, 1, (LPARAM)&r);
    put_rect("l.itemrect", n, &r, 0);
    put("l.tabstops", (int)SendMessageA(l, LB_SETTABSTOPS, 2, (LPARAM)stops));
    put("l.dir", (int)SendMessageA(l, LB_DIR, DDL_READWRITE, (LPARAM)"C:\\XPDIR\\*.TXT"));
    put("l.dircount", (int)SendMessageA(l, LB_GETCOUNT, 0, 0));
    put("l.addfile", (int)SendMessageA(l, LB_ADDFILE, 0, (LPARAM)"ADDED.TXT"));
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(l, LB_GETTEXT, 4, (LPARAM)buffer);
    put_text("l.dirtext", n, buffer, n > 0 ? n + 1 : 4);
    i = (int)SendMessageA(l, LB_ADDFILE, 0, (LPARAM)"C:\\XPDIR\\C.TXT");
    put("l.addfile2", i);
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(l, LB_GETTEXT, i, (LPARAM)buffer);
    put_text("l.addedtext", n, buffer, n > 0 ? n + 1 : 4);
}

static void extended(HWND l)
{
    int items[8] = {-1, -1, -1, -1, -1, -1, -1, -1}, n, i;
    SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"a");
    SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"b");
    SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"c");
    SendMessageA(l, LB_ADDSTRING, 0, (LPARAM)"d");
    put("x.range", (int)SendMessageA(l, LB_SELITEMRANGE, TRUE, MAKELPARAM(1, 2)));
    put("x.selcount", (int)SendMessageA(l, LB_GETSELCOUNT, 0, 0));
    n = (int)SendMessageA(l, LB_GETSELITEMS, 8, (LPARAM)items);
    length = 0;
    add("x.selitems");
    addf("=%d", n);
    for (i = 0; i < 4; ++i)
        addf(",%d", items[i]);
    flush();
}

static void edit(HWND e)
{
    char buffer[64];
    static const int stops[] = {16};
    DWORD start = 0xaaaa, end = 0xbbbb;
    RECT r, set = {2, 3, 100, 50};
    int n;
    put("e.settext", (int)SendMessageA(e, WM_SETTEXT, 0, (LPARAM)"First line\r\nSecond line"));
    SendMessageA(e, EM_SETSEL, 0, 5);
    put("e.replacesel", (int)SendMessageA(e, EM_REPLACESEL, FALSE, (LPARAM)"XY"));
    fill(buffer, sizeof buffer, '#');
    n = (int)SendMessageA(e, WM_GETTEXT, sizeof buffer, (LPARAM)buffer);
    put_text("e.text", n, buffer, n + 1);
    n = (int)SendMessageA(e, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    length = 0;
    add("e.getsel");
    addf("=%x", n);
    addf(",%x", (int)start);
    addf(",%x", (int)end);
    flush();
    fill((char *)&r, sizeof r, 0x77);
    n = (int)SendMessageA(e, EM_GETRECT, 0, (LPARAM)&r);
    put_rect("e.getrect", n, &r, 0);
    put("e.setrect", (int)SendMessageA(e, EM_SETRECT, 0, (LPARAM)&set));
    fill((char *)&r, sizeof r, 0x77);
    n = (int)SendMessageA(e, EM_GETRECT, 0, (LPARAM)&r);
    put_rect("e.getrect2", n, &r, 0);
    put("e.tabstops", (int)SendMessageA(e, EM_SETTABSTOPS, 1, (LPARAM)stops));
}

/* EM_GETLINE, last, and only with "getline" on the command line: in
   Windows 95 and 98 the sender of this message to an edit of another
   process ends with a fault in USER's thunk. */
static void getline(HWND e)
{
    char buffer[64];
    int n;
    const char *p;
    for (p = GetCommandLineA(); *p; ++p)
        if (p[0] == 'g' && p[1] == 'e' && p[2] == 't' && p[3] == 'l' && p[4] == 'i' && p[5] == 'n' && p[6] == 'e')
            break;
    if (!*p)
        return;
    SendMessageA(e, WM_SETTEXT, 0, (LPARAM)"First line\r\nSecond line");
    fill(buffer, sizeof buffer, '#');
    *(WORD *)buffer = 20;
    n = (int)SendMessageA(e, EM_GETLINE, 1, (LPARAM)buffer);
    put_text("e.getline", n, buffer, n + 2);
    fill(buffer, sizeof buffer, '#');
    *(WORD *)buffer = 4;
    n = (int)SendMessageA(e, EM_GETLINE, 0, (LPARAM)buffer);
    put_text("e.getlineshort", n, buffer, 6);
}

static void owner(HWND c, HWND l)
{
    DWORD value;
    int n;
    put("o.add0", (int)SendMessageA(c, CB_ADDSTRING, 0, 0x12345678));
    put("o.add1", (int)SendMessageA(c, CB_ADDSTRING, 0, 0x0000abcd));
    put("o.data", (int)SendMessageA(c, CB_GETITEMDATA, 1, 0));
    value = 0x55555555;
    n = (int)SendMessageA(c, CB_GETLBTEXT, 0, (LPARAM)&value);
    length = 0;
    add("o.text");
    addf("=%d", n);
    addf(",%x", (int)value);
    flush();
    put("o.find", (int)SendMessageA(c, CB_FINDSTRING, (WPARAM)-1, 0x0000abcd));
    put("o.select", (int)SendMessageA(c, CB_SELECTSTRING, (WPARAM)-1, 0x0000abcd));
    put("o.cursel", (int)SendMessageA(c, CB_GETCURSEL, 0, 0));
    put("ol.add0", (int)SendMessageA(l, LB_ADDSTRING, 0, 0x11223344));
    put("ol.add1", (int)SendMessageA(l, LB_ADDSTRING, 0, 0x00005566));
    value = 0x55555555;
    n = (int)SendMessageA(l, LB_GETTEXT, 1, (LPARAM)&value);
    length = 0;
    add("ol.text");
    addf("=%d", n);
    addf(",%x", (int)value);
    flush();
    put("ol.find", (int)SendMessageA(l, LB_FINDSTRING, (WPARAM)-1, 0x00005566));
}

static void minmax(HWND w)
{
    MINMAXINFO info;
    int n;
    fill((char *)&info, sizeof info, 0x11);
    info.ptMaxSize.x = 640;
    info.ptMaxSize.y = 480;
    n = (int)SendMessageA(w, WM_GETMINMAXINFO, 0, (LPARAM)&info);
    length = 0;
    add("m.minmax");
    addf("=%d", n);
    addf(",%d", info.ptMaxSize.x);
    addf(",%d", info.ptMaxSize.y);
    addf(",%d", info.ptMinTrackSize.x);
    addf(",%d", info.ptMinTrackSize.y);
    flush();
}

/* A fault of this program: its code and place, before it ends. */
static LONG WINAPI fault(EXCEPTION_POINTERS *e)
{
    length = 0;
    add("fault=");
    addf("%x", (int)e->ExceptionRecord->ExceptionCode);
    addf(",%x", (int)e->ContextRecord->Eip);
    addf(",%x", (int)e->ContextRecord->Esp);
    flush();
    return EXCEPTION_EXECUTE_HANDLER;
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA(), *p;
    char path[MAX_PATH], text[64];
    static STARTUPINFOA start;
    PROCESS_INFORMATION process;
    HWND server = 0;
    HANDLE file;
    DWORD n;
    int i;
    for (p = command; *p; ++p)
        if (p[0] == 's' && p[1] == 'e' && p[2] == 'r' && p[3] == 'v' && p[4] == 'e' && p[5] == 'r')
            serve();
    SetUnhandledExceptionFilter(fault);
    out = CreateFileA("C:\\XPROC.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    CreateDirectoryA("C:\\XPDIR", 0);
    file = CreateFileA("C:\\XPDIR\\A.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CloseHandle(file);
    file = CreateFileA("C:\\XPDIR\\B.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CloseHandle(file);
    file = CreateFileA("C:\\XPDIR\\C.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CloseHandle(file);
    GetModuleFileNameA(0, path, sizeof path);
    lstrcpyA(text, "XPROC.EXE server");
    start.cb = sizeof start;
    if (!CreateProcessA(path, text, 0, 0, FALSE, 0, 0, 0, &start, &process))
        put("server.started", 0);
    for (i = 0; i < 200 && !server; ++i)
    {
        Sleep(100);
        server = FindWindowA("XProcServer", 0);
    }
    put("server", server != 0);
    if (server)
    {
        Sleep(500);
        combo(GetDlgItem(server, 101));
        list(GetDlgItem(server, 102));
        edit(GetDlgItem(server, 103));
        owner(GetDlgItem(server, 104), GetDlgItem(server, 105));
        extended(GetDlgItem(server, 106));
        minmax(server);
        getline(GetDlgItem(server, 103));
        if (GetFileAttributesA("C:\\XP16.EXE") != 0xffffffff)
        {
            put("win16.started", WinExec("C:\\XP16.EXE", SW_SHOWNORMAL) > 31);
            for (i = 0; i < 200 && GetFileAttributesA("C:\\XP16OUT.TXT") == 0xffffffff; ++i)
                Sleep(100);
            Sleep(300);
            put("win16.done", GetFileAttributesA("C:\\XP16OUT.TXT") != 0xffffffff);
        }
        PostMessageA(server, WM_CLOSE, 0, 0);
        WaitForSingleObject(process.hProcess, 10000);
    }
    lstrcpyA(line, "done=1");
    length = lstrlenA(line);
    flush();
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    CloseHandle(out);
    ExitProcess(0);
}
