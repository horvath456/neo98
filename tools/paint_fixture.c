/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "capture_handshake.h"
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    int left, top, right, bottom;
} Rect;
typedef struct
{
    U dc, erase;
    Rect rect;
    U restore, incremental;
    unsigned char reserved[32];
} Paint;
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} Message;
typedef struct
{
    U style;
    U(__stdcall *proc)(U, U, U, U);
    int ce, we;
    U instance, icon, cursor, background;
    const char *menu, *name;
} Class;
API U RegisterClassA(const Class *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U BeginPaint(U, Paint *);
API U EndPaint(U, const Paint *);
API U GetUpdateRect(U, Rect *, U);
API U InvalidateRect(U, const Rect *, U);
API U ValidateRect(U, const Rect *);
API U UpdateWindow(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetClientRect(U, Rect *);
API U FillRect(U, const Rect *, U);
API U GetPixel(U, int, int);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U GetStockObject(U);
API U SelectObject(U, U);
API U TextOutA(U, int, int, const char *, int);
API U SetTextColor(U, U);
API U SetBkMode(U, int);
API U PatBlt(U, int, int, int, int, U);
API U PeekMessageA(Message *, U, U, U, U);
API U GetMessageA(Message *, U, U, U);
API U PostMessageA(U, U, U, U);
API U DispatchMessageA(const Message *);
API U TranslateMessage(const Message *);
API U MoveWindow(U, int, int, int, int, U);
API void Sleep(U);
API void ExitProcess(U);
static U paints, erases, mode, red, blue, reenter, child;
static Rect last;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(Rect a, Rect b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
static U __stdcall procedure(U h, U m, U wp, U lp)
{
    if (m == 0x14)
    {
        ++erases;
        if (reenter == 1)
        {
            Rect r = {90, 5, 100, 15};
            reenter = 0;
            check(InvalidateRect(h, &r, 1), 1601);
            Sleep(0);
        }
        if (reenter == 2)
        {
            reenter = 0;
            check(DestroyWindow(h), 1602);
            return 1;
        }
        if (mode == 3)
            return 0;
        return DefWindowProcA(h, m, wp, lp);
    }
    if (m == 0xf)
    {
        Paint ps;
        for (U i = 0; i < sizeof ps; ++i)
            ((unsigned char *)&ps)[i] = 0xab;
        U dc = BeginPaint(h, &ps);
        check(dc && dc == ps.dc, 1603);
        ++paints;
        last = ps.rect;
        check(ps.restore == 0 && ps.incremental == 0, 1604);
        for (U i = 16; i < 32; ++i)
            check(ps.reserved[i] == 0xab, 1605);
        if (mode == 0)
        {
            Rect all;
            GetClientRect(h, &all);
            check(FillRect(dc, &all, blue), 1607);
            SelectObject(dc, GetStockObject(11));
            SetTextColor(dc, 0xffffff);
            SetBkMode(dc, 1);
            check(TextOutA(dc, 10, 10, "WM_PAINT from x86 code", 21), 1608);
        }
        else
        {
            Rect all;
            GetClientRect(h, &all);
            check(FillRect(dc, &all, red), 1609);
            if (mode == 1)
                check(GetPixel(dc, 20, 20) == 0xffffffffu && GetPixel(dc, 11, 11) == 0xff, 1610);
            if (mode == 3)
                check(ps.erase == 1, 1611);
            else
                check(ps.erase == 0, 1612);
        }
        check(EndPaint(h, &ps), 1613);
        check(EndPaint(h, &ps), 1614); // Win98 returns TRUE after the release attempt.
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}
void run_paint_test(U instance)
{
    Class c = {0, procedure, 0, 0, instance, 0, 0, 6, 0, "NEO98PaintContract"};
    check(RegisterClassA(&c), 1615);
    red = CreateSolidBrush(0xff);
    blue = CreateSolidBrush(0x800000);
    check(red && blue, 1616);
    U h = CreateWindowExA(0, c.name, "Win98 paint protocol", 0x10cf0000, 40, 50, 308, 171, 0, 0, instance, 0);
    check(h, 1617);
    Rect all, update;
    check(GetClientRect(h, &all), 1618);
    Message msg;
    check(GetUpdateRect(h, &update, 0) && equal(all, update), 1618);
    check(PostMessageA(h, 0x401, 123, 0), 1619);
    check(PeekMessageA(&msg, h, 0, 0, 1) && msg.message == 0x401, 1620);
    check(PeekMessageA(&msg, h, 0xf, 0xf, 1) && msg.message == 0xf, 1621);
    check(GetMessageA(&msg, h, 0xf, 0xf) && msg.message == 0xf, 1622); // PM_REMOVE did not validate.
    DispatchMessageA(&msg);
    check(paints == 1 && erases == 1, 1623);
    check(!GetUpdateRect(h, &update, 0) && equal(update, (Rect){0, 0, 0, 0}), 1624);
    check(!PeekMessageA(&msg, h, 0xf, 0xf, 1), 1625);
    check(UpdateWindow(h) && paints == 1, 1626);
    // Clip a hole out of the pending region, then try to overpaint the entire client.
    mode = 1;
    Rect area = {10, 10, 60, 60}, hole = {15, 15, 40, 40};
    check(InvalidateRect(h, &area, 0) && ValidateRect(h, &hole), 1627);
    check(UpdateWindow(h) && paints == 2 && erases == 1 && equal(last, area), 1628);
    U dc = GetDC(h);
    check(dc, 1629);
    check(GetPixel(dc, 11, 11) == 0xff && GetPixel(dc, 20, 30) == 0x800000 &&
              GetPixel(dc, 80, 80) == 0x800000,
          1630);
    check(ReleaseDC(h, dc), 1631);
    // The host takes its picture of the composed client this x86 procedure drew.
    NEO98_WAIT_FOR_CAPTURE(Message);
    // The same client content survives recomposition and movement.
    check(MoveWindow(h, 70, 60, 308, 171, 1), 1632);
    Sleep(20);
    dc = GetDC(h);
    check(GetPixel(dc, 11, 11) == 0xff && GetPixel(dc, 80, 80) == 0x800000, 1633);
    ReleaseDC(h, dc);
    check(paints == 2, 1634);
    // Erase requested through GetUpdateRect leaves the update pending but clears fErase.
    mode = 2;
    check(InvalidateRect(h, &area, 1), 1635);
    check(GetUpdateRect(h, &update, 1) && erases == 2 && equal(update, area), 1636);
    check(UpdateWindow(h) && paints == 3 && erases == 2, 1637);
    // Reentrant erasing reinvalidates a separate area; EndPaint must preserve it.
    reenter = 1;
    check(InvalidateRect(h, &area, 1) && UpdateWindow(h), 1638);
    check(GetUpdateRect(h, &update, 0) && equal(update, (Rect){90, 5, 100, 15}), 1639);
    check(UpdateWindow(h) && !GetUpdateRect(h, 0, 0) && paints == 5, 1640);
    // Returning zero from WM_ERASEBKGND is visible in PAINTSTRUCT.fErase.
    mode = 3;
    check(InvalidateRect(h, &area, 1) && UpdateWindow(h), 1641);
    // The zero answer does not ask again: USER re-arms the send-erase
    // bit only for a window without WFWIN31COMPAT, and this one is 4.0. An
    // invalidation without erase therefore paints without WM_ERASEBKGND,
    // but retains fErase (WND+2Dh bit 4). WELCOME.EXE repainted
    // forever when the send-erase bit was rearmed too.
    U old_erases = erases;
    mode = 3;
    check(InvalidateRect(h, &area, 0) && UpdateWindow(h) && erases == old_erases, 1654);
    mode = 0;
    check(InvalidateRect(h, 0, 1) && UpdateWindow(h), 1642);
    check(!PeekMessageA(&msg, h, 0xf, 0xf, 1), 1643);
    // BeginPaint without an update has an empty clip, and EndPaint restores GetDC.
    Paint ps;
    dc = BeginPaint(h, &ps);
    check(dc && equal(ps.rect, (Rect){0, 0, 0, 0}), 1644);
    check(PatBlt(dc, 0, 0, all.right, all.bottom, 0x42) && EndPaint(h, &ps), 1645);
    dc = GetDC(h);
    check(GetPixel(dc, 80, 80) == 0x800000, 1646);
    ReleaseDC(h, dc);
    child = CreateWindowExA(0, c.name, "child", 0x50000000, 180, 70, 80, 50, h, 55, instance, 0);
    check(child, 1647);
    check(UpdateWindow(child), 1648);
    check(DestroyWindow(h), 1649);
    // Destruction from WM_ERASEBKGND retires the outstanding DC without a dangling write.
    // Visible: InvalidateRect on a hidden window does nothing (USER
    // gives up ), and this case needs the erase of BeginPaint.
    h = CreateWindowExA(0, c.name, "destroy in erase", 0x10000000, 660, 500, 120, 80, 0, 0, instance, 0);
    check(h, 1650);
    GetUpdateRect(h, 0, 0); /* settles the exposure of the new window */
    check(ValidateRect(h, 0) && !GetUpdateRect(h, 0, 0) && InvalidateRect(h, 0, 1), 1653);
    reenter = 2;
    for (U i = 0; i < sizeof ps; ++i)
        ((unsigned char *)&ps)[i] = 0xcd;
    check(!BeginPaint(h, &ps) && ps.dc == 0xcdcdcdcd, 1651);
    check(DeleteObject(red) && DeleteObject(blue), 1652);
}
