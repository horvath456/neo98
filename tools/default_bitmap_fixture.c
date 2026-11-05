/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the default bitmap of one memory DC goes into another one.
   Win98 gives every memory DC the same 1 by 1 monochrome stock bitmap (GDI
   the shared stock bitmap), which SelectObject lets into any DC. After
   a colour change DESK.CPL draws its monitor picture in a fresh memory DC and
   frees it again with the bitmap another DC answered on its SelectObject
   in Windows 98. If that fails, the picture stays selected
   in the drawing DC and the SS_BITMAP label that shows it copies from an
   empty DC: the Settings page showed a black monitor. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U, int, int);
API U SelectObject(U, U);
API U DeleteDC(U);
API U DeleteObject(U);
API U SetPixel(U, int, int, U);
API U GetPixel(U, int, int);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }

void mainCRTStartup(void)
{
    U screen = GetDC(0);
    check(screen != 0, 10);
    U picture = CreateCompatibleBitmap(screen, 16, 16), pristine = CreateCompatibleBitmap(screen, 16, 16);
    check(picture && pristine, 11);
    /* The long-lived DC of DESK (6023 in the trace) with its own copy. */
    U keeper = CreateCompatibleDC(screen);
    U keeper_default = SelectObject(keeper, pristine);
    check(keeper_default != 0, 12);
    /* The drawing DC takes the picture... */
    U drawing = CreateCompatibleDC(screen);
    check(SelectObject(drawing, picture) != 0, 15);
    check(SetPixel(drawing, 3, 3, 0x0000ff) != 0xffffffffu, 16);
    /* and gives it back for the keeper's default bitmap. */
    check(SelectObject(drawing, keeper_default) == picture, 17);
    /* The keeper takes its own default bitmap back as well. */
    check(SelectObject(keeper, keeper_default) == pristine, 18);
    /* The picture is free: a third DC, the label's, can select it. */
    U label = CreateCompatibleDC(screen);
    check(SelectObject(label, picture) != 0, 19);
    check((GetPixel(label, 3, 3) & 0xffffff) == 0x0000ff, 20);
    DeleteDC(label);
    DeleteDC(drawing);
    DeleteDC(keeper);
    check(DeleteObject(picture) && DeleteObject(pristine), 21);
    ReleaseDC(0, screen);
    ExitProcess(0);
}
