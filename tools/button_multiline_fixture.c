/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * Buttons with BS_MULTILINE and one whose single line is wider than the
 * button (tools/test_button_multiline_host.py), measured with the same
 * program in Windows 95 OSR2 and Windows 98 SE. A topmost popup at 0,0
 * holds ten buttons in DEFAULT_GUI_FONT. When they have painted, without
 * the focus, the program copies every button's client area off the screen
 * and writes, per button, the rows of the check cell
 * (the columns of the cell that are not the face colour) and the ink of
 * the caption area as runs "first-last:left-right" of rows with the same
 * leftmost and rightmost point that is not the face colour. Then the check
 * box of the third gets the focus ("focus" says whether it got it), and the
 * bounding box of its caption area's ink, its focus rectangle, is written
 * as "b2.focus=left,top,right,bottom". C:\BUTTONML.TXT
 * (and COM1, when it opens) gets name=value lines. With the argument
 * "hold" the window stays ten seconds more, for a picture; C:\BUTTONML.BMP
 * is the window as the program copied it.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static char line[4096];
static int length;

static void add(const char *text)
{
    while (*text && length < (int)sizeof line - 3)
        line[length++] = *text++;
}
static void number(int value)
{
    char digits[16];
    wsprintfA(digits, "%d", value);
    add(digits);
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

struct Button { const char *text; DWORD style; int x, y, cx, cy; };
/* Passwords > User Profiles of Windows 95 lays out its two radio buttons
   and two check boxes as 192 x 20, 192 x 30 and 180 x 20 dialog units with
   BS_TOP and BS_MULTILINE: 288 x 32, 288 x 48 and 270 x 32 pixels in
   MS Sans Serif 8. */
static const struct Button buttons[] = {
    {"All &users of this PC use the same preferences and desktop settings.",
     BS_AUTORADIOBUTTON | BS_TOP | BS_MULTILINE, 4, 4, 288, 32},
    {"Users can &customize their preferences and desktop settings.  Windows switches to your personal settings "
     "whenever you log in.", BS_AUTORADIOBUTTON | BS_TOP | BS_MULTILINE, 4, 40, 288, 48},
    {"Include &desktop icons and Network Neighborhood contents in user settings",
     BS_AUTOCHECKBOX | BS_TOP | BS_MULTILINE, 4, 92, 270, 32},
    {"A check box of two lines, centred in its height", BS_AUTOCHECKBOX | BS_MULTILINE, 4, 128, 200, 48},
    {"A check box of two lines at the bottom of its height", BS_AUTOCHECKBOX | BS_BOTTOM | BS_MULTILINE,
     4, 180, 200, 48},
    {"Right aligned lines with the &text at the left", BS_AUTORADIOBUTTON | BS_RIGHT | BS_LEFTTEXT | BS_MULTILINE,
     300, 4, 200, 40},
    {"A &push button of two lines", BS_PUSHBUTTON | BS_MULTILINE, 300, 48, 120, 40},
    {"This text is wider than its button and stays on one line", BS_AUTORADIOBUTTON, 300, 92, 150, 20},
    {"Centred lines of a check box with BS_CENTER", BS_AUTOCHECKBOX | BS_CENTER | BS_MULTILINE, 300, 116, 200, 40},
    {"Far more text than three lines of this small check box can ever show at all", BS_AUTOCHECKBOX | BS_MULTILINE,
     300, 160, 120, 20},
};
#define COUNT (sizeof buttons / sizeof buttons[0])
static HWND windows[COUNT];

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

/* With `box`, only the bounding box of the caption area's ink: the
   focus rectangle. The display driver of Windows 95 and 98 in a virtual
   machine (16 colours) draws its points solid, so only its place is
   compared. */
static void measure(int index, int box)
{
    const struct Button *b = &buttons[index];
    const DWORD kind = b->style & 15;
    const int check = kind != BS_PUSHBUTTON && kind != BS_DEFPUSHBUTTON;
    const int left_text = (b->style & BS_LEFTTEXT) != 0;
    const COLORREF face = GetSysColor(COLOR_BTNFACE);
    RECT client;
    static BITMAPINFO info;
    DWORD *bits = 0;
    HDC screen, memory;
    HBITMAP bitmap, old;
    int width, height, x, y, cell_top = -1, cell_bottom = -1, run_first = -1, run_left = 0, run_right = 0;
    int text_left, text_right;
    GetClientRect(windows[index], &client);
    width = client.right;
    height = client.bottom;
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    screen = GetDC(windows[index]);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, 0, 0);
    old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY);
    GdiFlush();
    ReleaseDC(windows[index], screen);
#define PIXEL(px, py) (bits[(py) * width + (px)] & 0xffffff)
#define FACE (((face & 0xff) << 16) | (face & 0xff00) | ((face >> 16) & 0xff))
    /* The cell: 13 columns at the side, or more with a larger cell; the
       caption area begins where a column of the face colour follows it. */
    text_left = 0;
    text_right = width;
    if (check)
    {
        int columns = 0;
        if (left_text)
        {
            for (x = width - 1; x >= 0; --x)
            {
                int marked = 0;
                for (y = 0; y < height; ++y)
                    marked |= PIXEL(x, y) != FACE;
                if (!marked && columns)
                    break;
                columns += marked;
            }
            text_right = x;
            for (y = 0; y < height; ++y)
                for (x = text_right + 1; x < width; ++x)
                    if (PIXEL(x, y) != FACE)
                    {
                        if (cell_top < 0)
                            cell_top = y;
                        cell_bottom = y;
                    }
        }
        else
        {
            for (x = 0; x < width; ++x)
            {
                int marked = 0;
                for (y = 0; y < height; ++y)
                    marked |= PIXEL(x, y) != FACE;
                if (!marked && columns)
                    break;
                columns += marked;
            }
            text_left = x;
            for (y = 0; y < height; ++y)
                for (x = 0; x < text_left; ++x)
                    if (PIXEL(x, y) != FACE)
                    {
                        if (cell_top < 0)
                            cell_top = y;
                        cell_bottom = y;
                    }
        }
    }
    else
    {
        /* A push button: inside its frame of two points. */
        text_left = 3;
        text_right = width - 3;
    }
    if (box)
    {
        int l = width, t = -1, r = -1, bottom = -1;
        for (y = 0; y < height; ++y)
            for (x = text_left; x < text_right; ++x)
                if (PIXEL(x, y) != FACE)
                {
                    if (t < 0)
                        t = y;
                    bottom = y;
                    if (x < l)
                        l = x;
                    if (x > r)
                        r = x;
                }
        wsprintfA(line, "b%d.focus=%d,%d,%d,%d", index, l, t, r, bottom);
        length = lstrlenA(line);
        flush();
        SelectObject(memory, old);
        DeleteObject(bitmap);
        DeleteDC(memory);
        return;
    }
    wsprintfA(line, "b%d.cell=", index);
    length = lstrlenA(line);
    number(cell_top);
    add(",");
    number(cell_bottom);
    flush();
    wsprintfA(line, "b%d.ink=", index);
    length = lstrlenA(line);
    for (y = check ? 0 : 3; y <= (check ? height : height - 3); ++y)
    {
        int l = -1, r = -1;
        if (y < (check ? height : height - 3))
            for (x = text_left; x < text_right; ++x)
                if (PIXEL(x, y) != FACE)
                {
                    if (l < 0)
                        l = x;
                    r = x;
                }
        if (run_first >= 0 && (l != run_left || r != run_right))
        {
            if (run_left >= 0)
            {
                number(run_first);
                add("-");
                number(y - 1);
                add(":");
                number(run_left);
                add("-");
                number(run_right);
                add(" ");
            }
            run_first = -1;
        }
        if (run_first < 0)
        {
            run_first = y;
            run_left = l;
            run_right = r;
        }
    }
    flush();
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

/* The whole window as C:\BUTTONML.BMP, to look at. */
static void picture(HWND window)
{
    static BITMAPINFO info;
    static BITMAPFILEHEADER file;
    RECT client;
    DWORD *bits = 0, n;
    HDC screen, memory;
    HBITMAP bitmap, old;
    HANDLE handle;
    GetClientRect(window, &client);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = client.right;
    info.bmiHeader.biHeight = client.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    screen = GetDC(window);
    memory = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, (void **)&bits, 0, 0);
    old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, client.right, client.bottom, screen, 0, 0, SRCCOPY);
    GdiFlush();
    ReleaseDC(window, screen);
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof file + sizeof info.bmiHeader;
    file.bfSize = file.bfOffBits + client.right * client.bottom * 4;
    handle = CreateFileA("C:\\BUTTONML.BMP", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(handle, &file, sizeof file, &n, 0);
    WriteFile(handle, &info.bmiHeader, sizeof info.bmiHeader, &n, 0);
    WriteFile(handle, bits, client.right * client.bottom * 4, &n, 0);
    CloseHandle(handle);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

void mainCRTStartup(void)
{
    static WNDCLASSA klass;
    HWND parent;
    unsigned i;
    DWORD n;
    out = CreateFileA("C:\\BUTTONML.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = GetModuleHandleA(0);
    klass.lpszClassName = "ButtonMultiline";
    klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    klass.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&klass);
    parent = CreateWindowExA(WS_EX_TOPMOST, "ButtonMultiline", "ButtonMultiline", WS_POPUP | WS_VISIBLE, 0, 0, 520,
                             240, 0, 0, klass.hInstance, 0);
    for (i = 0; i < COUNT; ++i)
    {
        windows[i] = CreateWindowA("BUTTON", buttons[i].text, WS_CHILD | WS_VISIBLE | buttons[i].style,
                                   buttons[i].x, buttons[i].y, buttons[i].cx, buttons[i].cy, parent,
                                   (HMENU)(100 + i), klass.hInstance, 0);
        SendMessageA(windows[i], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
    }
    SetCursorPos(639, 479);
    SetForegroundWindow(parent);
    SetFocus(parent);
    pump();
    /* Let the shell finish its start (and a box of its own go behind the
       topmost window), then paint everything again and measure. */
    for (i = 0; i < 30; ++i)
    {
        Sleep(100);
        pump();
    }
    SetForegroundWindow(parent);
    SetFocus(parent);
    RedrawWindow(parent, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump();
    Sleep(300);
    pump();
    for (i = 0; i < COUNT; ++i)
        measure(i, 0);
    SetFocus(windows[2]);
    UpdateWindow(windows[2]);
    pump();
    Sleep(300);
    pump();
    lstrcpyA(line, "focus=");
    length = lstrlenA(line);
    number(GetFocus() == windows[2]);
    flush();
    measure(2, 1);
    picture(parent);
    lstrcpyA(line, "done=1");
    length = lstrlenA(line);
    flush();
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    CloseHandle(out);
    /* "BUTTONML.EXE hold" stays for a picture. */
    {
        const char *command = GetCommandLineA();
        for (; *command; ++command)
            if (command[0] == 'h' && command[1] == 'o' && command[2] == 'l' && command[3] == 'd')
                for (i = 0; i < 100; ++i)
                {
                    Sleep(100);
                    pump();
                }
    }
    ExitProcess(0);
}
