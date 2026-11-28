/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * The children of a message box (tools/test_message_box_children_host.py),
 * measured with the same program in Windows 95 OSR2 and Windows 98 SE.
 * Windows builds the box from a dialog template: its buttons, a STATIC with
 * SS_ICON for the icon and a STATIC for the text. For five boxes the program
 * writes what a thread's WH_CBT hook sees at HCBT_CREATEWND (class, id,
 * style, place, name) and, from a timer while the box runs, what
 * EnumChildWindows finds (class, id, style, place in the box's client area,
 * GetWindowText and WM_GETTEXT), what GetDlgItem and GetDlgItemText answer for
 * the text's id 0FFFFh and the icon's, and then ends the box with the
 * command of its last button. It sets the work area of a 640 x 480 screen
 * with Windows' taskbar first. C:\MBCHILD.TXT (and COM1, when it opens) gets
 * name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static char line[2048];
static int length, box_index, created, enumerated;
static HWND box;

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
/* A text with its bytes outside 20h..7Eh as \xx. */
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
        WriteFile(com, line, length, &n, 0);
    length = 0;
}
static void begin(const char *name)
{
    length = 0;
    addf("m%d.", box_index);
    add(name);
    add("=");
}

static LRESULT CALLBACK hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND)
    {
        const CREATESTRUCTA *cs = ((CBT_CREATEWNDA *)lp)->lpcs;
        char name[16];
        begin("");
        length--;
        addf("cbt%d=", created++);
        if (HIWORD((DWORD)cs->lpszClass))
            add(cs->lpszClass);
        else
            addf("#%d", LOWORD((DWORD)cs->lpszClass));
        addf(",id=%d", (int)(DWORD)cs->hMenu & 0xffff);
        addf(",style=%08x", (int)cs->style);
        addf(",%d", cs->x);
        addf(",%d", cs->y);
        addf(",%d", cs->cx);
        addf(",%d", cs->cy);
        add(",name=");
        if (!cs->lpszName)
            add("null");
        else if (!HIWORD((DWORD)cs->lpszName))
            addf("#%d", LOWORD((DWORD)cs->lpszName));
        else
        {
            int n = 0;
            while (n < 12 && cs->lpszName[n])
                ++n;
            lstrcpynA(name, cs->lpszName, n + 1);
            quoted(name, n);
        }
        flush();
    }
    return CallNextHookEx(0, code, wp, lp);
}

static BOOL CALLBACK child(HWND window, LPARAM unused)
{
    char text[300], klass[64];
    RECT r;
    POINT p;
    int n;
    (void)unused;
    if (GetParent(window) != box)
        return TRUE;
    GetClassNameA(window, klass, sizeof klass);
    GetWindowRect(window, &r);
    p.x = r.left;
    p.y = r.top;
    ScreenToClient(box, &p);
    begin("");
    length--;
    addf("child%d=", enumerated++);
    add(klass);
    addf(",id=%d", GetDlgCtrlID(window) & 0xffff);
    addf(",style=%08x", (int)GetWindowLongA(window, GWL_STYLE));
    addf(",%d", p.x);
    addf(",%d", p.y);
    addf(",%d", r.right - r.left);
    addf(",%d", r.bottom - r.top);
    n = GetWindowTextA(window, text, sizeof text);
    add(",text=");
    quoted(text, n);
    n = (int)SendMessageA(window, WM_GETTEXT, sizeof text, (LPARAM)text);
    addf(",gettext=%d", n);
    addf(",length=%d", GetWindowTextLengthA(window));
    flush();
    return TRUE;
}

struct Box { const char *text, *caption; UINT style; int button; };
static const struct Box boxes[] = {
    {"Saved.", "Plain", MB_OK, IDCANCEL},
    {"Do you want to save the changes?", "Question", MB_YESNO | MB_ICONQUESTION, IDNO},
    {"The disk is full. Free some space on the disk and try again, or save the document on another disk. "
     "This text is long enough to need several lines in its box.\r\nAnd a line of its own.",
     "Exclamation", MB_OKCANCEL | MB_ICONEXCLAMATION, IDCANCEL},
    {"Right aligned & with an ampersand", "Hand", MB_RETRYCANCEL | MB_ICONHAND | MB_RIGHT, IDCANCEL},
    {"Line one\r\nLine two\r\nLine three\r\nLine four", "Asterisk", MB_ABORTRETRYIGNORE | MB_ICONASTERISK, IDIGNORE},
};
static int last_button;
static void CALLBACK timer(HWND window, UINT message, UINT_PTR id, DWORD time)
{
    char text[300];
    RECT client;
    HWND item;
    int n;
    (void)window;
    (void)message;
    (void)time;
    /* The box of this turn, by its caption; until it is there (another
       program's window may be in front), the timer goes on. */
    box = FindWindowA("#32770", boxes[box_index].caption);
    if (!box)
        return;
    KillTimer(0, id);
    GetClassNameA(box, text, sizeof text);
    begin("box");
    add(text);
    GetClientRect(box, &client);
    addf(",client=%d", client.right);
    addf("x%d", client.bottom);
    GetWindowRect(box, &client);
    addf(",window=%d", client.left);
    addf(",%d", client.top);
    addf(",%d", client.right - client.left);
    addf(",%d", client.bottom - client.top);
    {
        RECT work;
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
        addf(",work=%d", work.left);
        addf(",%d", work.top);
        addf(",%d", work.right);
        addf(",%d", work.bottom);
    }
    flush();
    enumerated = 0;
    EnumChildWindows(box, child, 0);
    item = GetDlgItem(box, 0xffff);
    begin("dlgitem.ffff");
    addf("%d", item != 0);
    flush();
    n = GetDlgItemTextA(box, 0xffff, text, sizeof text);
    begin("dlgitemtext.ffff");
    quoted(text, n);
    flush();
    begin("dlgitem.10");
    addf("%d", GetDlgItem(box, 10) != 0);
    flush();
    begin("dlgitem.20");
    addf("%d", GetDlgItem(box, 20) != 0);
    flush();
    PostMessageA(box, WM_COMMAND, last_button, 0);
}



void mainCRTStartup(void)
{
    HHOOK cbt;
    unsigned i;
    DWORD n;
    out = CreateFileA("C:\\MBCHILD.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    /* The work area of a 640 x 480 screen with the taskbar of Windows at
       the bottom, so that the boxes stand where they stand in Windows. */
    {
        RECT work = {0, 0, 640, 452};
        SystemParametersInfoA(SPI_SETWORKAREA, 0, &work, 0);
    }
    cbt = SetWindowsHookExA(WH_CBT, hook, 0, GetCurrentThreadId());
    for (i = 0; i < sizeof boxes / sizeof boxes[0]; ++i)
    {
        int result;
        box_index = (int)i;
        created = 0;
        last_button = boxes[i].button;
        SetTimer(0, 0, 1500, timer);
        result = MessageBoxA(0, boxes[i].text, boxes[i].caption, boxes[i].style | MB_SETFOREGROUND);
        begin("result");
        addf("%d", result);
        flush();
    }
    UnhookWindowsHookEx(cbt);
    lstrcpyA(line, "done=1");
    length = lstrlenA(line);
    flush();
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    CloseHandle(out);
    ExitProcess(0);
}
