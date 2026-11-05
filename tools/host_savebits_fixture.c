/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Saved bits under a CS_SAVEBITS window (tools/test_savebits_restore_host.py).
// A program that draws its picture directly through GetDC and whose WM_PAINT
// only erases, as MahJong 2000 does: window W at 50,40, 500 by 400, erases
// green. Keys, to whichever window has the focus; the title of W names the
// last step:
// 1 the picture: W red all over, through GetDC and PatBlt -> "board"
// 2 dialog D (CS_SAVEBITS, caption, a push button) at 150,120, 250 by 180,
// created or shown again -> "dialog"
// 3 InvalidateRect of W at 400,300..480,380, away from D; the paint
// erases that square -> "painted"
// 4 D hidden -> "hidden"
// 5 W draws a blue block at 120,100, 60 by 40 through GetDC: under D
// -> "under"
// 6 D destroyed -> "destroyed"
// 7 red board, D, overlapping saved upper dialog -> "overlapping"
// 8 D hidden below the upper dialog -> "lower-hidden"
// 9 upper dialog destroyed, foreground returned to W -> "upper-destroyed"
// A upper dialog destroyed first, D still visible -> "upper-first"
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
API U GetClientRect(U, int *);
API U SetWindowTextA(U, const char *);
API U GetDC(U);
API U ReleaseDC(U, U);
API U InvalidateRect(U, const int *, U);
API U ShowWindow(U, U);
API U DestroyWindow(U);
API U SetWindowPos(U, U, int, int, int, int, U);
API U SetFocus(U);
API U SetForegroundWindow(U);
API U CreateSolidBrush(U);
API U SelectObject(U, U);
API U PatBlt(U, int, int, int, int, U);

static U window, dialog, upper, reusable, instance, red, green, blue, asked;

static void block(U brush, int x, int y, int w, int h)
{
    const U dc = GetDC(window);
    const U old = SelectObject(dc, brush);
    PatBlt(dc, x, y, w, h, 0x00f00021); // PATCOPY
    SelectObject(dc, old);
    ReleaseDC(window, dc);
}

static void key(U k)
{
    switch (k) {
    case '1':
        block(red, 0, 0, 500, 400);
        SetWindowTextA(window, "board");
        break;
    case '2':
        if (dialog) ShowWindow(dialog, 5);
        else {
            // WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU, owned by W.
            dialog = CreateWindowExA(0, "HOST_SAVEBITS_D", "Dialog", 0x90c80000, 150, 120, 250, 180, window, 0, instance, 0);
            // WS_CHILD | WS_VISIBLE.
            CreateWindowExA(0, "BUTTON", "OK", 0x50000000, 80, 90, 80, 30, dialog, 1, instance, 0);
        }
        SetWindowTextA(window, "dialog");
        break;
    case '3': {
        const int r[4] = {400, 300, 480, 380};
        asked = 1;
        InvalidateRect(window, r, 1);
        break;
    }
    case '4':
        ShowWindow(dialog, 0);
        SetFocus(window);
        SetWindowTextA(window, "hidden");
        break;
    case '5':
        block(blue, 120, 100, 60, 40);
        SetWindowTextA(window, "under");
        break;
    case '6':
        DestroyWindow(dialog);
        dialog = 0;
        SetFocus(window);
        SetWindowTextA(window, "destroyed");
        break;
    case '7':
        if (dialog) { DestroyWindow(dialog); dialog = 0; }
        block(red, 0, 0, 500, 400);
        key('2');
        upper = CreateWindowExA(0, "HOST_SAVEBITS_D", "Upper", 0x90c80000,
                               100, 160, 350, 100, window, 0, instance, 0);
        SetWindowTextA(window, "overlapping");
        break;
    case '8':
        ShowWindow(dialog, 0);
        SetFocus(upper);
        SetWindowTextA(window, "lower-hidden");
        break;
    case '9':
        DestroyWindow(upper); upper = 0;
        SetForegroundWindow(window); SetFocus(window);
        SetWindowTextA(window, "upper-destroyed");
        break;
    case 'A':
        DestroyWindow(upper); upper = 0;
        SetFocus(dialog);
        SetWindowTextA(window, "upper-first");
        break;
    case 'B':
        if (dialog) { DestroyWindow(dialog); dialog = 0; }
        if (reusable) { DestroyWindow(reusable); reusable = 0; }
        // Both records exist before either popup paints. Creating another
        // window between hide and show would exercise the hide/show recovery path.
        dialog = CreateWindowExA(0, "HOST_SAVEBITS_D", "First", 0x80c80000,
                                150, 120, 250, 180, window, 0, instance, 0);
        reusable = CreateWindowExA(0, "HOST_SAVEBITS_D", "Reused", 0x80c80000,
                                  150, 120, 250, 180, window, 0, instance, 0);
        block(red, 0, 0, 500, 400);
        ShowWindow(dialog, 5);
        SetWindowTextA(window, "reuse-ready");
        break;
    case 'C':
    case 'D':
    case 'E':
        // No paint, DC query, message pump or allocation between these calls.
        if (k == 'C') ShowWindow(dialog, 0);
        if (k == 'D') SetWindowPos(dialog, 0, 0, 0, 0, 0, 0x97);
        if (k == 'E') { DestroyWindow(dialog); dialog = 0; }
        if (k == 'D') SetWindowPos(reusable, 0, 0, 0, 0, 0, 0x57);
        else ShowWindow(reusable, 5);
        SetWindowTextA(window, "reuse-shown");
        break;
    case 'G':
    case 'H':
        if (k == 'G') SetWindowPos(dialog, 0, 450, 120, 250, 180, 0x15);
        else {
            SetWindowPos(reusable, 0, 250, 120, 150, 180, 0x14);
            SetWindowPos(dialog, 0, 150, 120, 100, 180, 0x16);
        }
        SetWindowPos(reusable, 0, 0, 0, 0, 0, 0x57);
        SetWindowTextA(window, "reuse-shown");
        break;
    case 'F':
        ShowWindow(reusable, 0);
        SetForegroundWindow(window); SetFocus(window);
        SetWindowTextA(window, "reuse-hidden");
        break;
    }
}

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x100) { key(wp); return 0; } // WM_KEYDOWN
    if (h == window) {
        if (m == 0x14) { // WM_ERASEBKGND
            int r[4];
            GetClientRect(h, r);
            FillRect(wp, r, green);
            return 1;
        }
        if (m == 0xf) { // WM_PAINT: nothing but the erase
            PAINT p;
            BeginPaint(h, &p);
            EndPaint(h, &p);
            if (asked) SetWindowTextA(window, "painted");
            asked = 0;
            return 0;
        }
        if (m == 2) ExitProcess(0); // WM_DESTROY
    }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    red = CreateSolidBrush(0x000000ff);
    green = CreateSolidBrush(0x0000ff00);
    blue = CreateSolidBrush(0x00ff0000);
    instance = GetModuleHandleA(0);
    CLASS c = {0, (U)proc, 0, 0, instance, 0, 0, 0, 0, "HOST_SAVEBITS_W"};
    if (!RegisterClassA(&c)) ExitProcess(7940);
    // CS_SAVEBITS; COLOR_BTNFACE + 1 as the class brush.
    CLASS d = {0x800, (U)proc, 0, 0, instance, 0, 0, 16, 0, "HOST_SAVEBITS_D"};
    if (!RegisterClassA(&d)) ExitProcess(7941);
    // WS_POPUP | WS_VISIBLE.
    window = CreateWindowExA(0, "HOST_SAVEBITS_W", "start", 0x90000000, 50, 40, 500, 400, 0, 0, instance, 0);
    if (!window) ExitProcess(7942);
    SetForegroundWindow(window);
    SetFocus(window);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    ExitProcess(0);
}
