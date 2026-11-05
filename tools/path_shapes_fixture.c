/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The closed shapes inside an open path, on a window DC, on the screen DC and
   on a memory DC. Win98's GDI.EXE records only lines, curves, polygons and
   text into a path (segment 33). Every other drawing entry tests the open bit
   of DC+7Ah and answers FALSE without recording or drawing:
       RECTANGLE (after : an empty rectangle, left == right
              or top == bottom, answers TRUE before the test)
       the arc worker behind ELLIPSE, ARC, PIE, CHORD and
              ROUNDRECT
   GDI32.DLL hands all six to GDI.EXE as WORD arguments through the thunk at
    (Rectangle, Ellipse, RoundRect,
   Pie, Arc, Chord ). The answer is the same for
   every kind of DC; nothing sets a last error.
   Exit 0: all as Win98. Otherwise 1000 * (kind + 1) + 10 * call + check,
   check 1 result, 2 recorded points, 3 last error, 4 pixel. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void SetLastError(U);
API U GetLastError(void);
API U GetModuleHandleA(const char *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U GetDC(U);
API int ReleaseDC(U, U);
API U DestroyWindow(U);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U, int, int);
API U SelectObject(U, U);
API U GetStockObject(int);
API U PatBlt(U, int, int, int, int, U);
API U GetPixel(U, int, int);
API U DeleteDC(U);
API U DeleteObject(U);
API U BeginPath(U);
API U EndPath(U);
API U AbortPath(U);
API int GetPath(U, void *, void *, int);
API U MoveToEx(U, int, int, void *);
API U LineTo(U, int, int);
API U Rectangle(U, int, int, int, int);
API U Ellipse(U, int, int, int, int);
API U RoundRect(U, int, int, int, int, int, int);
API U Arc(U, int, int, int, int, int, int, int, int);
API U Pie(U, int, int, int, int, int, int, int, int);
API U Chord(U, int, int, int, int, int, int, int, int);
API U Polygon(U, const int *, int);
API U Polyline(U, const int *, int);

enum { CallRectangle, CallEllipse, CallRoundRect, CallArc, CallPie, CallChord,
       CallEmptyRectangle, CallFlatRectangle, CallPolygon, CallPolyline, Calls };

static const int triangle[] = {4, 4, 16, 4, 10, 16};

static U shape(U dc, int call)
{
    switch (call)
    {
    case CallRectangle: return Rectangle(dc, 2, 2, 20, 20);
    case CallEllipse: return Ellipse(dc, 2, 2, 20, 20);
    case CallRoundRect: return RoundRect(dc, 2, 2, 20, 20, 4, 4);
    case CallArc: return Arc(dc, 2, 2, 20, 20, 2, 2, 20, 20);
    case CallPie: return Pie(dc, 2, 2, 20, 20, 2, 2, 20, 20);
    case CallChord: return Chord(dc, 2, 2, 20, 20, 2, 2, 20, 20);
    case CallEmptyRectangle: return Rectangle(dc, 5, 2, 5, 20);
    case CallFlatRectangle: return Rectangle(dc, 2, 7, 20, 7);
    case CallPolygon: return Polygon(dc, triangle, 3);
    default: return Polyline(dc, triangle, 3);
    }
}

/* The result and the points the call adds to a path of one line (2 points). */
static void expected(int call, U *result, int *points)
{
    *result = call >= CallEmptyRectangle;
    *points = 2 + (call >= CallPolygon ? 3 : 0);
}

static void check(int ok, int kind, int call, int what)
{
    if (!ok) ExitProcess(1000 * (kind + 1) + 10 * call + what);
}

static void run(U dc, int kind, U memory)
{
    for (int call = 0; call < Calls; ++call)
    {
        U want;
        int points;
        expected(call, &want, &points);
        if (memory) PatBlt(dc, 0, 0, 32, 32, 0x00FF0062); /* WHITENESS */
        check(BeginPath(dc) != 0, kind, call, 5);
        MoveToEx(dc, 1, 1, 0);
        LineTo(dc, 24, 1);
        SetLastError(0x1234);
        const U result = shape(dc, call);
        const U error = GetLastError();
        check(EndPath(dc) != 0, kind, call, 6);
        const int count = GetPath(dc, 0, 0, 0);
        AbortPath(dc);
        check(result == want, kind, call, 1);
        check(count == points, kind, call, 2);
        check(error == 0x1234, kind, call, 3);
        /* Nothing reaches the surface while the path is open. */
        if (memory)
            check(GetPixel(dc, 2, 2) == 0xFFFFFF && GetPixel(dc, 10, 10) == 0xFFFFFF &&
                  GetPixel(dc, 10, 4) == 0xFFFFFF, kind, call, 4);
    }
}

void mainCRTStartup(void)
{
    /* WS_OVERLAPPEDWINDOW | WS_VISIBLE */
    const U window = CreateWindowExA(0, "STATIC", "path shapes", 0x10cf0000u, 40, 40, 200, 150, 0, 0,
                                     GetModuleHandleA(0), 0);
    check(window != 0, 0, 0, 9);
    const U window_dc = GetDC(window);
    check(window_dc != 0, 0, 0, 8);
    run(window_dc, 0, 0);
    ReleaseDC(window, window_dc);

    const U screen_dc = GetDC(0);
    check(screen_dc != 0, 1, 0, 8);
    run(screen_dc, 1, 0);

    const U memory_dc = CreateCompatibleDC(screen_dc);
    const U bitmap = CreateCompatibleBitmap(screen_dc, 32, 32);
    check(memory_dc && bitmap, 2, 0, 8);
    const U old = SelectObject(memory_dc, bitmap);
    SelectObject(memory_dc, GetStockObject(4)); /* BLACK_BRUSH: a fill would show */
    run(memory_dc, 2, 1);
    SelectObject(memory_dc, old);
    DeleteDC(memory_dc);
    DeleteObject(bitmap);
    ReleaseDC(0, screen_dc);
    DestroyWindow(window);
    ExitProcess(0);
}
