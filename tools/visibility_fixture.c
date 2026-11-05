/* SPDX-License-Identifier: GPL-2.0-or-later
 * triage: four ways a Delphi/VB-style child window becomes visible and
 * gets its first WM_ERASEBKGND/WM_PAINT, without the guest ever calling
 * ShowWindow on the child itself:
 * a) CreateWindowExA with WS_VISIBLE already in the style.
 * b) created hidden, shown with SetWindowPos(SWP_SHOWWINDOW|...).
 * c) created hidden, shown with SetWindowLong(GWL_STYLE,..|WS_VISIBLE)
 * followed only by SetWindowPos(SWP_FRAMECHANGED), no InvalidateRect.
 * d) created with WS_VISIBLE under a parent that is itself still hidden;
 * the parent gets ShowWindow only afterwards.
 * Each child counts its own WM_ERASEBKGND/WM_PAINT and fills its client with
 * its own colour so a screenshot can tell the four apart. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { int left, top, right, bottom; } Rect;
typedef struct { U dc, erase; Rect rect; U restore, incremental; unsigned char reserved[32]; } Paint;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } Message;
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U SetWindowPos(U, U, int, int, int, int, U);
API int GetWindowLongA(U, int);
API int SetWindowLongA(U, int, int);
API U BeginPaint(U, Paint *);
API U EndPaint(U, const Paint *);
API U GetClientRect(U, Rect *);
API U FillRect(U, const Rect *, U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U PeekMessageA(Message *, U, U, U, U);
API U GetMessageA(Message *, U, U, U);
API U DispatchMessageA(const Message *);
static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U paints[4], erases[4];
/* COLORREF 0x00bbggrr: pure red, green, blue, magenta. */
static const U colours[4] = {0x0000ff, 0x00ff00, 0xff0000, 0xff00ff};
static U __stdcall child_proc(U h, U m, U wp, U lp)
{
    int index = GetWindowLongA(h, -21);
    if (m == 0x14)
    {
        if (index >= 0 && index < 4) ++erases[index];
        return 1; /* handled: no default background erase to overwrite our fill */
    }
    if (m == 0xf)
    {
        Paint ps;
        U dc = BeginPaint(h, &ps);
        check(dc != 0, 7701);
        if (index >= 0 && index < 4)
        {
            ++paints[index];
            Rect all;
            check(GetClientRect(h, &all), 7702);
            U brush = CreateSolidBrush(colours[index]);
            check(brush != 0, 7703);
            check(FillRect(dc, &all, brush), 7704);
            check(DeleteObject(brush), 7705);
        }
        check(EndPaint(h, &ps), 7706);
        return 0;
    }
    return DefWindowProcA(h, m, wp, lp);
}
static U __stdcall frame_proc(U h, U m, U wp, U lp) { return DefWindowProcA(h, m, wp, lp); }
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U frame_class[10] = {0, (U)frame_proc, 0, 0, instance, 0, 0, 16, 0, (U)"Neo98VisFrame"};
    U child_class[10] = {0, (U)child_proc, 0, 0, instance, 0, 0, 16, 0, (U)"Neo98VisChild"};
    check(RegisterClassA(frame_class), 7710);
    check(RegisterClassA(child_class), 7711);
    /* Frame1 is visible from the start; it hosts children a, b and c. */
    U frame1 = CreateWindowExA(0, "Neo98VisFrame", "Frame1", 0x10cf0000, 20, 20, 360, 260, 0, 0, instance, 0);
    check(frame1 != 0, 7712);
    /* a) WS_VISIBLE already in the CreateWindowExA style. */
    U child_a = CreateWindowExA(0, "Neo98VisChild", "ChildA", 0x50000000, 10, 10, 90, 50, frame1, 0, instance, 0);
    check(child_a != 0, 7713);
    SetWindowLongA(child_a, -21, 0);
    /* b) created hidden, shown through SetWindowPos(SWP_SHOWWINDOW). */
    U child_b = CreateWindowExA(0, "Neo98VisChild", "ChildB", 0x40000000, 10, 70, 90, 50, frame1, 0, instance, 0);
    check(child_b != 0, 7715);
    SetWindowLongA(child_b, -21, 1);
    check(SetWindowPos(child_b, 0, 0, 0, 0, 0, 0x40 | 0x02 | 0x01 | 0x04), 7716); /* SHOWWINDOW|NOMOVE|NOSIZE|NOZORDER */
    /* c) created hidden, style flips WS_VISIBLE on through SetWindowLong, then
     * only SetWindowPos(SWP_FRAMECHANGED) asks for a redraw; no explicit
     * InvalidateRect from this guest at all. */
    U child_c = CreateWindowExA(0, "Neo98VisChild", "ChildC", 0x40000000, 10, 130, 90, 50, frame1, 0, instance, 0);
    check(child_c != 0, 7717);
    SetWindowLongA(child_c, -21, 2);
    int style_c = GetWindowLongA(child_c, -16);
    check((style_c & 0x10000000) == 0, 7718); /* not visible yet */
    SetWindowLongA(child_c, -16, style_c | 0x10000000);
    check((GetWindowLongA(child_c, -16) & 0x10000000) != 0, 7719);
    check(SetWindowPos(child_c, 0, 0, 0, 0, 0, 0x20 | 0x02 | 0x01 | 0x04 | 0x10), 7720); /* FRAMECHANGED|NOMOVE|NOSIZE|NOZORDER|NOACTIVATE */
    /* d) child created WS_VISIBLE under a still-hidden parent; the parent is
     * shown only afterwards. */
    U frame2 = CreateWindowExA(0, "Neo98VisFrame", "Frame2", 0x00cf0000, 420, 20, 220, 150, 0, 0, instance, 0);
    check(frame2 != 0, 7721);
    U child_d = CreateWindowExA(0, "Neo98VisChild", "ChildD", 0x50000000, 10, 10, 90, 50, frame2, 0, instance, 0);
    check(child_d != 0, 7722);
    SetWindowLongA(child_d, -21, 3);
    check(paints[3] == 0, 7723); /* nothing painted while the parent is still hidden */
    /* ShowWindow's return value is the window's *previous* visibility, not a
     * success flag; frame2 was hidden, so it is correctly zero here. */
    ShowWindow(frame2, 5); /* SW_SHOW on the parent only */
    check((GetWindowLongA(frame2, -16) & 0x10000000) != 0, 7724);
    /* Drain every pending message, including the synthesized WM_PAINT of every
     * window of this thread, the way an ordinary message loop would. */
    Message msg;
    for (U round = 0; round < 8; ++round)
        while (PeekMessageA(&msg, 0, 0, 0, 1)) DispatchMessageA(&msg);
    check(erases[0] >= 1 && paints[0] >= 1, 7730); /* a */
    check(erases[1] >= 1 && paints[1] >= 1, 7731); /* b */
    check(erases[2] >= 1 && paints[2] >= 1, 7732); /* c: fails without the SWP_FRAMECHANGED fix */
    check(erases[3] >= 1 && paints[3] >= 1, 7733); /* d */
    /* All checks passed. Stay alive with the windows on screen (never post
     * WM_QUIT) so the host's screenshot, taken after its time limit, still
     * shows what every child painted; ExitProcess here would tear the
     * windows down first and the probe would photograph the bare desktop. */
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
