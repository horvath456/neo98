/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Two processes show windows on one desktop. Message 0x401 exits immediately
// without explicit window destruction, exercising process cleanup.
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
API char *GetCommandLineA(void);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U InvalidateRect(U, const void *, U);

static const char klass[] = "RECOMPOSE_WINDOW";

static void check(U value, U code) { if (!value) ExitProcess(code); }

static U dying;

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x401) { ExitProcess(0); return 0; }
    // Message 0x402 exits inside WM_PAINT between BeginPaint and EndPaint.
    // Pending paint state must not block presentation for the remaining
    // session.
    if (m == 0x402) { dying = 1; check(InvalidateRect(h, 0, 1), 7813); return 0; }
    if (m == 0x0f && dying)
    {
        PAINT paint;
        BeginPaint(h, &paint);
        ExitProcess(0);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const U second = command[0] == 'B';
    const U instance = GetModuleHandleA(0);
    CLASS c = {0, (U)proc, 0, 0, instance, 0, 0, 6, 0, klass};
    check(RegisterClassA(&c), 7810);
    // WS_OVERLAPPEDWINDOW | WS_VISIBLE. The COLOR_WINDOW+1 class brush paints
    // a white client area over the teal desktop.
    const U window = CreateWindowExA(0, klass, second ? "B" : "A", 0x10cf0000,
                                     second ? 300 : 20, second ? 120 : 60,
                                     second ? 260 : 240, second ? 180 : 160, 0, 0, instance, 0);
    check(window, 7811);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(7812);
}
