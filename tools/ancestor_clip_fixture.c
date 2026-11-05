/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// a child is clipped by the siblings above its parent.
//
// Explorer changes folders like this: the new SHELLDLL_DefView is created
// beside the old one and shown on top of it (SetWindowPos HWND_TOP with
// SWP_SHOWWINDOW); the old view then takes its list view back from its web
// view, stretches it over its whole width and is destroyed. On Win98 none of
// that shows: the old view has WS_CLIPSIBLINGS, the new one covers it, and a
// child's visible region lies inside its parent's. The composition of the
// port clipped a child only against its own siblings, so the old list showed
// through the new view for a moment.
//
// The frame holds the old view A (yellow) with its list A1 (red) on the
// right half. 0x401 creates the new view B (blue) on top of A and stretches
// A1 over the whole of A; the frame's title then reads "moved".
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
typedef struct { U dc, erase; int left, top, right, bottom; U restore, incremental;
                 unsigned char reserved[32]; } PAINT;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(MSG *, U, U, U);
API U TranslateMessage(MSG *);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U FillRect(U, const int *, U);
API U CreateSolidBrush(U);
API U GetWindowLongA(U, int);
API U SetWindowPos(U, U, int, int, int, int, U);
API U UpdateWindow(U);
API U SetWindowTextA(U, const char *);

static const char frame_class[] = "ANCESTOR_CLIP_FRAME", view_class[] = "ANCESTOR_CLIP_VIEW";
static U instance, frame, old_view, list, brushes[4];

static void check(U value, U code) { if (!value) ExitProcess(code); }

static U __stdcall view(U h, U m, U wp, U lp)
{
    if (m == 0x0f)
    {
        PAINT paint;
        const U dc = BeginPaint(h, &paint);
        const U id = GetWindowLongA(h, -12);
        FillRect(dc, &paint.left, brushes[id & 3]);
        EndPaint(h, &paint);
        return 0;
    }
    if (m == 0x14) return 1;
    return DefWindowProcA(h, m, wp, lp);
}

static U __stdcall frame_proc(U h, U m, U wp, U lp)
{
    if (m == 0x401)
    {
        // WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, hidden, as SHELL32
        // creates its view (style 46010000h without WS_TABSTOP).
        const U fresh = CreateWindowExA(0, view_class, "B", 0x46000000, 0, 0, 300, 200, h, 3, instance, 0);
        check(fresh, 7831);
        check(SetWindowPos(fresh, 0, 0, 0, 0, 0, 0x43), 7832);
        UpdateWindow(fresh);
        check(SetWindowPos(list, 0, 0, 0, 300, 200, 0x14), 7833);
        UpdateWindow(list);
        SetWindowTextA(h, "moved");
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    instance = GetModuleHandleA(0);
    brushes[0] = CreateSolidBrush(0x00ffffff);
    brushes[1] = CreateSolidBrush(0x0000ffff);   // yellow: the old view
    brushes[2] = CreateSolidBrush(0x000000ff);   // red: its list
    brushes[3] = CreateSolidBrush(0x00ff0000);   // blue: the new view
    CLASS f = {0, (U)frame_proc, 0, 0, instance, 0, 0, 6, 0, frame_class};
    CLASS v = {0, (U)view, 0, 0, instance, 0, 0, 0, 0, view_class};
    check(RegisterClassA(&f) && RegisterClassA(&v), 7820);
    frame = CreateWindowExA(0, frame_class, "frame", 0x12cf0000, 40, 40, 360, 280, 0, 0, instance, 0);
    check(frame, 7821);
    old_view = CreateWindowExA(0, view_class, "A", 0x56000000, 0, 0, 300, 200, frame, 1, instance, 0);
    check(old_view, 7822);
    list = CreateWindowExA(0, view_class, "A1", 0x54000000, 150, 0, 150, 200, old_view, 2, instance, 0);
    check(list, 7823);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(0);
}
