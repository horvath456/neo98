/* SPDX-License-Identifier: GPL-2.0-or-later
 * EndDialog from a control's WM_SETFONT while CreateDialogIndirect builds a
 * modeless dialog (the yield_dialog fixture, measured on Win98 SE). */
#include <windows.h>
#include "wine/test.h"

static int mode, control_hits, font_hits, init_hits;
static HWND parent_seen;

static LRESULT CALLBACK control_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CREATE)
    {
        ++control_hits;
        if (mode == 1 && control_hits == 1) { parent_seen = GetParent(h); EndDialog(parent_seen, 0x1234); }
    }
    if (m == WM_SETFONT) ++font_hits;
    return DefWindowProcA(h, m, w, l);
}
static INT_PTR CALLBACK dialog_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_INITDIALOG) { ++init_hits; return TRUE; }
    return FALSE;
}
static void word(BYTE *b, int *at, WORD v) { b[(*at)++] = (BYTE)v; b[(*at)++] = (BYTE)(v >> 8); }
static void dword(BYTE *b, int *at, DWORD v) { word(b, at, LOWORD(v)); word(b, at, HIWORD(v)); }
static void string(BYTE *b, int *at, const char *s) { do word(b, at, (BYTE)*s); while (*s++); }
static int make(BYTE *b, int setfont)
{
    int at = 0, item;
    dword(b, &at, 0x90c80080 | (setfont ? DS_SETFONT : 0)); dword(b, &at, 0); word(b, &at, 2);
    word(b, &at, 0); word(b, &at, 0); word(b, &at, 100); word(b, &at, 60);
    word(b, &at, 0); word(b, &at, 0); string(b, &at, "YieldDialog");
    if (setfont) { word(b, &at, 8); string(b, &at, "MS Sans Serif"); }
    for (item = 0; item < 2; ++item)
    {
        while (at & 3) b[at++] = 0;
        dword(b, &at, 0x50000000); dword(b, &at, 0);
        word(b, &at, 5); word(b, &at, (WORD)(5 + item * 16)); word(b, &at, 40); word(b, &at, 14);
        word(b, &at, (WORD)(11 + item)); string(b, &at, "Yield98Ctl"); string(b, &at, item ? "two" : "one"); word(b, &at, 0);
    }
    return at;
}

START_TEST(dialog_end_create)
{
    static BYTE data[512];
    WNDCLASSA cls = {0};
    HWND plain, ended;
    cls.lpfnWndProc = control_proc; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "Yield98Ctl";
    ok(RegisterClassA(&cls) != 0, "class\n");
    make(data, 0);
    mode = 0; control_hits = font_hits = init_hits = 0;
    plain = CreateDialogIndirectParamA(cls.hInstance, (LPCDLGTEMPLATEA)data, 0, dialog_proc, 0);
    trace("without DS_SETFONT: controls=%d fonts=%d init=%d\n", control_hits, font_hits, init_hits);
    ok(plain && control_hits == 2 && init_hits == 1, "plain dialog\n");
    ok(font_hits == 0, "no WM_SETFONT without DS_SETFONT (%d)\n", font_hits);
    DestroyWindow(plain);
    make(data, 1);
    mode = 0; control_hits = font_hits = init_hits = 0;
    plain = CreateDialogIndirectParamA(cls.hInstance, (LPCDLGTEMPLATEA)data, 0, dialog_proc, 0);
    trace("with DS_SETFONT: controls=%d fonts=%d init=%d\n", control_hits, font_hits, init_hits);
    ok(plain && control_hits == 2 && init_hits == 1 && font_hits == 2, "WM_SETFONT to each control with DS_SETFONT\n");
    DestroyWindow(plain);
    mode = 1; control_hits = font_hits = init_hits = 0; parent_seen = 0;
    ended = CreateDialogIndirectParamA(cls.hInstance, (LPCDLGTEMPLATEA)data, 0, dialog_proc, 0);
    trace("EndDialog in WM_CREATE: result=%p parent=%p IsWindow=%d visible=%d controls=%d fonts=%d init=%d\n", ended,
          parent_seen, IsWindow(parent_seen), IsWindowVisible(parent_seen), control_hits, font_hits, init_hits);
    ok(parent_seen != NULL, "EndDialog reached\n");
    ok(control_hits == 2 && init_hits == 1 && ended == parent_seen && IsWindow(parent_seen),
       "modeless build continues after EndDialog, dialog kept\n");
    if (IsWindow(parent_seen)) DestroyWindow(parent_seen);
}
