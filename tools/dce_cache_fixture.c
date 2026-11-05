/* SPDX-License-Identifier: GPL-2.0-or-later */
/* USER's DCE cache as a program sees it (USER GetDCEx,
 * ReleaseDC). A window of an ordinary class gets a new cache DC with
 * default attributes on every GetDC, and BeginPaint does not hand out the DC a
 * GetDC holds; CS_OWNDC keeps one DC with its attributes for the window,
 * CS_CLASSDC one for the class. Exit code 0 is a pass, otherwise the number of
 * the failed check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, int);
API U UpdateWindow(U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U WindowFromDC(U);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U SetTextColor(U, U);
API U GetTextColor(U);
API int SaveDC(U);
API U SetPixel(U, int, int, U);
API U GetPixel(U, int, int);
API U InvalidateRect(U, const int *, U);
API int GetClipBox(U, int *);

static U failed, painted;

static void check(int good, U code)
{
    if (!good && !failed) failed = code;
}

/* WM_PAINT of the ordinary class: a GetDC inside the paint is a second DC. */
static U __stdcall common_procedure(U hwnd, U message, U wp, U lp)
{
    if (message != 15) return DefWindowProcA(hwnd, message, wp, lp);
    U ps[16];
    const U paint = BeginPaint(hwnd, ps);
    check(paint != 0, 18401);
    const U other = GetDC(hwnd);
    check(other && other != paint, 18402);
    check(WindowFromDC(paint) == hwnd && WindowFromDC(other) == hwnd, 18403);
    /* A cache DC starts without saved states and with its own attributes. */
    check(SaveDC(other) == 1, 18404);
    SetTextColor(other, 0x0000ff);
    check(GetTextColor(paint) == 0, 18405);
    /* Both DCs draw on the same window. */
    SetPixel(paint, 2, 2, 0x00ff00);
    SetPixel(other, 3, 3, 0xff0000);
    check(GetPixel(paint, 3, 3) == 0xff0000 && GetPixel(other, 2, 2) == 0x00ff00, 18406);
    check(ReleaseDC(hwnd, other) == 1, 18407);
    EndPaint(hwnd, ps);
    painted = 1;
    return 0;
}

static U __stdcall plain_procedure(U hwnd, U message, U wp, U lp)
{
    return DefWindowProcA(hwnd, message, wp, lp);
}

static U window(const char *klass, U style, U procedure, int x)
{
    U fields[10] = {style, procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)klass};
    RegisterClassA(fields);
    return CreateWindowExA(0, klass, klass, 0x90000000u, x, 20, 120, 80, 0, 0, fields[4], 0);
}

void mainCRTStartup(void)
{
    /* Ordinary class. */
    const U common = window("DceCommon", 0, (U)common_procedure, 20);
    check(common != 0, 18400);
    UpdateWindow(common);
    check(painted, 18408);
    U first = GetDC(common), second = GetDC(common);
    check(first && second && first != second, 18410);
    SetTextColor(first, 0x0000ff);
    check(GetTextColor(second) == 0, 18411);
    /* Out of order, as a program with two threads releases them. */
    check(ReleaseDC(common, first) == 1 && ReleaseDC(common, second) == 1, 18412);
    check(ReleaseDC(common, first) == 0, 18413);
    first = GetDC(common);
    check(first && GetTextColor(first) == 0, 18414);
    ReleaseDC(common, first);

    /* CS_OWNDC 20h: one DC with its attributes for the life of the window. */
    const U own = window("DceOwn", 0x20, (U)plain_procedure, 160);
    check(own != 0, 18420);
    first = GetDC(own);
    SetTextColor(first, 0x0000ff);
    check(ReleaseDC(own, first) == 1, 18421);
    second = GetDC(own);
    check(second == first && GetTextColor(second) == 0x0000ff, 18422);
    ReleaseDC(own, second);
    check(WindowFromDC(first) == own, 18423);
    {
        U ps[16];
        const U paint = BeginPaint(own, ps);
        check(paint == first && GetTextColor(paint) == 0x0000ff, 18424);
        EndPaint(own, ps);
    }
    /* EndPaint releases with fEndPaint, and
     * drops the update region BeginPaint intersected from an own DC too.
     * A program that draws through the kept DC after the paint
     * draws on the whole window, not only where the last paint was. */
    {
        const int small[4] = {10, 10, 30, 20};
        InvalidateRect(own, small, 1);
        U ps[16];
        const U paint = BeginPaint(own, ps);
        int box[4] = {0, 0, 0, 0};
        GetClipBox(paint, box);
        check(box[0] == 10 && box[1] == 10 && box[2] == 30 && box[3] == 20, 18425);
        EndPaint(own, ps);
        box[0] = box[1] = box[2] = box[3] = -1;
        GetClipBox(first, box);
        check(box[0] == 0 && box[1] == 0 && box[2] == 120 && box[3] == 80, 18426);
    }

    /* CS_CLASSDC 40h: one DC for every window of the class. */
    const U a = window("DceClass", 0x40, (U)plain_procedure, 300);
    const U b = CreateWindowExA(0, "DceClass", "b", 0x90000000u, 440, 20, 120, 80, 0, 0,
                                GetModuleHandleA(0), 0);
    check(a && b, 18430);
    first = GetDC(a);
    SetTextColor(first, 0x00ff00);
    ReleaseDC(a, first);
    second = GetDC(b);
    check(second == first && GetTextColor(second) == 0x00ff00, 18431);
    check(WindowFromDC(second) == b, 18432);
    ReleaseDC(b, second);

    DestroyWindow(b);
    DestroyWindow(a);
    DestroyWindow(own);
    DestroyWindow(common);
    ExitProcess(failed);
}
