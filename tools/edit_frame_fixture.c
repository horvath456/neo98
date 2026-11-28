/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * An EDIT control subclassed to draw its own border
 * (tools/test_edit_frame_host.py), measured with the same program in
 * Windows 95 OSR2 and Windows 98 SE. The subclass hands WM_NCPAINT on to
 * the edit's procedure and then frames the window in red, two pixels wide,
 * through GetWindowDC. The program reads the screen at the edit's left edge
 * and its top edge and writes whether the pixels are red, and how many
 * WM_NCPAINT the edit got, after each of: the first paint; a popup laid over
 * the edit and taken away again; the parent redrawn with RDW_FRAME and
 * RDW_ALLCHILDREN; new text in the edit; the edit given the focus.
 * With "move" on its command line it measures the cost instead: a popup
 * moves across the edit by a pixel every 10 ms for four seconds, and the
 * edit's WM_NCPAINT are counted. C:\EDITFRM.TXT (and COM1, when it opens)
 * gets name=value lines.
 */
#include <windows.h>

static HANDLE out = INVALID_HANDLE_VALUE, com = INVALID_HANDLE_VALUE;
static WNDPROC old_procedure;
static int frames;
static HWND edit;

static void put(const char *name, int value)
{
    char text[100];
    DWORD n;
    wsprintfA(text, "%s=%d\r\n", name, value);
    WriteFile(out, text, lstrlenA(text), &n, 0);
    if (com != INVALID_HANDLE_VALUE)
    {
        WriteFile(com, text, lstrlenA(text), &n, 0);
        FlushFileBuffers(com);
    }
}
static void pump(int milliseconds)
{
    MSG m;
    int i;
    for (i = 0; i < milliseconds / 50; ++i)
    {
        while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(50);
    }
}

static LRESULT CALLBACK subclass(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    LRESULT result = CallWindowProcA(old_procedure, w, message, wp, lp);
    if (message == WM_NCPAINT)
    {
        HDC dc = GetWindowDC(w);
        RECT r;
        HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
        ++frames;
        GetWindowRect(w, &r);
        OffsetRect(&r, -r.left, -r.top);
        FrameRect(dc, &r, red);
        InflateRect(&r, -1, -1);
        FrameRect(dc, &r, red);
        DeleteObject(red);
        ReleaseDC(w, dc);
    }
    return result;
}

/* Whether the border shows red: a pixel of the left edge, half way down,
   and one of the top edge, half way across, both on the outer line. */
static void look(const char *name)
{
    char key[64];
    RECT r;
    HDC screen = GetDC(0);
    COLORREF left, top;
    GetWindowRect(edit, &r);
    left = GetPixel(screen, r.left, (r.top + r.bottom) / 2);
    top = GetPixel(screen, (r.left + r.right) / 2, r.top);
    ReleaseDC(0, screen);
    wsprintfA(key, "%s.left", name);
    put(key, left == RGB(255, 0, 0));
    wsprintfA(key, "%s.top", name);
    put(key, top == RGB(255, 0, 0));
    wsprintfA(key, "%s.ncpaint", name);
    put(key, frames > 0);
    frames = 0;
}

void mainCRTStartup(void)
{
    static WNDCLASSA klass;
    HWND parent, popup;
    DWORD n;
    out = CreateFileA("C:\\EDITFRM.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    SetCursorPos(639, 479);
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = GetModuleHandleA(0);
    klass.lpszClassName = "EditFrame";
    klass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    klass.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&klass);
    parent = CreateWindowExA(WS_EX_TOPMOST, "EditFrame", "EditFrame", WS_POPUP | WS_CAPTION | WS_VISIBLE, 40, 40, 300,
                             160, 0, 0, klass.hInstance, 0);
    edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "text", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 30, 200, 26,
                           parent, (HMENU)100, klass.hInstance, 0);
    old_procedure = (WNDPROC)SetWindowLongA(edit, GWL_WNDPROC, (LONG)subclass);
    SetWindowPos(edit, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    SetForegroundWindow(parent);
    pump(2500);
    /* "EDITFRM.EXE move": the cost. A popup moves across the edit by a
       pixel every 10 ms for four seconds; the edit's WM_NCPAINT are
       counted. */
    {
        const char *p;
        for (p = GetCommandLineA(); *p; ++p)
            if (p[0] == 'm' && p[1] == 'o' && p[2] == 'v' && p[3] == 'e')
            {
                int i;
                frames = 0;
                popup = CreateWindowExA(WS_EX_TOPMOST, "EditFrame", "", WS_POPUP | WS_BORDER | WS_VISIBLE, 0, 75, 120,
                                        60, 0, 0, klass.hInstance, 0);
                pump(500);
                SetWindowTextA(parent, "EditFrameMoving");
                for (i = 0; i < 400; ++i)
                {
                    MSG m;
                    SetWindowPos(popup, 0, i, 75, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE))
                        DispatchMessageA(&m);
                    Sleep(10);
                }
                put("moves", i);
                put("move.ncpaint", frames);
                SetWindowTextA(parent, "EditFrameMoved");
                pump(1500);
                ExitProcess(0);
            }
    }
    look("first");
    /* A popup over the edit, and away again. */
    popup = CreateWindowExA(WS_EX_TOPMOST, "EditFrame", "", WS_POPUP | WS_BORDER | WS_VISIBLE, 50, 60, 180, 60, 0, 0,
                            klass.hInstance, 0);
    pump(800);
    DestroyWindow(popup);
    pump(1500);
    look("uncovered");
    RedrawWindow(parent, 0, 0, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    pump(1500);
    look("redrawn");
    SetWindowTextA(edit, "new text in the edit");
    pump(1500);
    look("text");
    SetFocus(edit);
    pump(1500);
    look("focus");
    put("done", 1);
    if (com != INVALID_HANDLE_VALUE)
        WriteFile(com, "MEASURE_DONE\r\n", 14, &n, 0);
    CloseHandle(out);
    ExitProcess(0);
}
