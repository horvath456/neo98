/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 public-API contract for the native USER nonclient port.
 * Expected rectangles come from USER under the shared
 * Win98 metric defaults. No copy of the implementation is linked into this guest.
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    int l, t, r, b;
} Rect;
typedef struct
{
    int x, y;
} Point;
typedef struct
{
    U dc, erase;
    Rect rect;
    U restore, incremental;
    unsigned char reserved[32];
} Paint;
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U GetClientRect(U, Rect *);
API U GetSystemMetrics(int);
API U GetWindowRect(U, Rect *);
API U MapWindowPoints(U, U, Point *, U);
API U AdjustWindowRect(Rect *, U, U);
API U AdjustWindowRectEx(Rect *, U, U, U);
API U SetWindowLongA(U, int, U);
API U GetWindowLongA(U, int);
API U SetWindowPos(U, U, int, int, int, int, U);
API U ShowWindow(U, int);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetPixel(U, int, int);
API U ValidateRect(U, const Rect *);
API U GetUpdateRect(U, Rect *, U);
API U BeginPaint(U, Paint *);
API U EndPaint(U, const Paint *);
API U SendMessageA(U, U, U, U);
API U CreateMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U GetMenu(U);
API U SetMenu(U, U);
API U DrawMenuBar(U);
API U EnableMenuItem(U, U, U);
API U DestroyMenu(U);
API void ExitProcess(U);

enum
{
    CAPTION_FRAME = 0x00cf0000,
    CHILD_FRAME_SCROLL = 0x40b00000,
    FRAME_CHANGED = 0x37, /* NOSIZE | NOMOVE | NOZORDER | NOACTIVATE | FRAMECHANGED */
    SIZE_ONLY = 0x16      /* NOMOVE | NOZORDER | NOACTIVATE */
};
static const char class_name[] = "NEO98NonclientContract";
static U instance;

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static U window(U extended, U style, int x, int y, int width, int height, U parent, U menu)
{
    return CreateWindowExA(extended, class_name, "Original USER geometry", style, x, y, width, height, parent,
                           menu, instance, 0);
}
static int equals(const Rect *r, int left, int top, int right, int bottom)
{
    return r->l == left && r->t == top && r->r == right && r->b == bottom;
}
static void client(U h, int width, int height, U code)
{
    Rect r;
    check(GetClientRect(h, &r) && equals(&r, 0, 0, width, height), code);
}
static void outer(U h, int left, int top, int right, int bottom, U code)
{
    Rect r;
    check(GetWindowRect(h, &r) && equals(&r, left, top, right, bottom), code);
}
static U delta(int x, int y) { return ((U)y << 16) | ((U)x & 65535); }
static void origin(U h, int x, int y, U code)
{
    Point points[2] = {{0, 0}, {13, -7}};
    check(MapWindowPoints(h, 0, points, 2) == delta(x, y) && points[0].x == x && points[0].y == y &&
              points[1].x == x + 13 && points[1].y == y - 7,
          code);
    check(MapWindowPoints(0, h, points, 2) == delta(-x, -y) && points[0].x == 0 && points[0].y == 0 &&
              points[1].x == 13 && points[1].y == -7,
          code);
}

static void adjustment(void)
{
    Rect r = {0, 0, 100, 60};
    check(AdjustWindowRect(&r, CAPTION_FRAME, 0) == 1 && equals(&r, -4, -23, 104, 64), 3551);
    r = (Rect){0, 0, 100, 60};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 0, 0) == 1 && equals(&r, -4, -23, 104, 64), 3552);
    r = (Rect){0, 0, 100, 60};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 1, 0) == 1 && equals(&r, -4, -43, 104, 64), 3553);
    r = (Rect){0, 0, 100, 60};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 0, 0x80) == 1 && equals(&r, -4, -21, 104, 64), 3554);
    /* The original adjustment does not reserve either scrollbar. */
    r = (Rect){0, 0, 100, 60};
    check(AdjustWindowRectEx(&r, CHILD_FRAME_SCROLL, 0, 0x200) == 1 && equals(&r, -3, -3, 103, 63), 3555);
    /* USER32 copies four low WORDs in, then sign-extends them out. */
    r = (Rect){0x12340001, 0x23450002, 0x34560065, 0x4567003e};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 0x10000, 0) == 1 && equals(&r, -3, -21, 105, 66), 3556);
    r = (Rect){0, 0, 100, 60};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 0x10001, 0) == 1 && equals(&r, -4, -43, 104, 64), 3557);
    r = (Rect){0x12348000, 0x1234ffff, 0x56747fff, 0x76540000};
    check(AdjustWindowRect(&r, 0x80000000u, 0x10000) == 1 && equals(&r, -32768, -1, 32767, 0), 3558);
    /* Inflation wraps as signed 16-bit rectangle arithmetic. */
    r = (Rect){-32767, -32767, 32767, 32767};
    check(AdjustWindowRectEx(&r, CAPTION_FRAME, 0, 0) == 1 && equals(&r, 32765, 32746, -32765, -32765), 3559);
}

static void tiny_windows(void)
{
    /* HSCROLL needs height >17; VSCROLL needs width >=17. */
    static const int cases[][4] = {
        {17, 17, 0, 17}, {18, 18, 1, 1}, {16, 18, 16, 1}, {18, 16, 1, 16}, {0, 18, 0, 18}};
    for (U i = 0; i < sizeof cases / sizeof *cases; ++i)
    {
        U h = window(0, 0x80300000u, 20, 30, cases[i][0], cases[i][1], 0, 0);
        check(h != 0, 3590 + i);
        client(h, cases[i][2], cases[i][3], 3590 + i);
        origin(h, 20, 30, 3590 + i);
        check(DestroyWindow(h), 3590 + i);
    }
    U h = window(0x200, 0x80000000u, 20, 30, 4, 4, 0, 0);
    check(h != 0, 3595);
    client(h, 0, 0, 3596);
    origin(h, 22, 32, 3597);
    check(SetWindowPos(h, 0, 0, 0, 3, 4, SIZE_ONLY), 3598);
    client(h, 3, 4, 3599);
    origin(h, 20, 30, 3600);
    check(DestroyWindow(h), 3601);
    /* Overlapped creation enforces the default minimum tracking size. */
    h = window(0, CAPTION_FRAME, 20, 30, 4, 10, 0, 0);
    check(h != 0, 3602);
    client(h, GetSystemMetrics(34) - 8, GetSystemMetrics(35) - 27, 3603);
    origin(h, 24, 53, 3604);
    check(DestroyWindow(h), 3605);
}

static void menu_geometry(void)
{
    U menu = CreateMenu();
    check(menu != 0, 3606);
    for (U i = 0; i < 6; ++i)
        check(AppendMenuA(menu, 0, 100 + i, "MMMMMMMM"), 3607);
    U h = window(0, CAPTION_FRAME, 40, 50, 120, 260, 0, 0);
    check(h && !GetMenu(h), 3608);
    client(h, 112, 233, 3609);
    check(SetMenu(h, menu) && GetMenu(h) == menu && DrawMenuBar(h), 3610);
    Rect narrow, wide, extra;
    check(GetClientRect(h, &narrow) && narrow.l == 0 && narrow.t == 0 && narrow.r == 112, 3611);
    /* Font measurement is the native provider. Regardless of its glyph widths,
     * multiple ordinary rows cost 19 pixels each plus one border pixel total. */
    int menu_height = 233 - narrow.b;
    check(menu_height >= 39 && menu_height <= 115 && (menu_height - 1) % 19 == 0, 3612);
    origin(h, 44, 73 + menu_height, 3613);
    check(SetWindowPos(h, 0, 0, 0, 700, 260, SIZE_ONLY) && DrawMenuBar(h), 3614);
    check(GetClientRect(h, &wide) && equals(&wide, 0, 0, 692, 213) && wide.b > narrow.b, 3615);
    origin(h, 44, 93, 3616);
    /* DrawMenuBar is SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOSIZE |
     * SWP_NOMOVE) in Win98 (USER over ). A menu change that keeps
     * the bar's height leaves the client area valid; its pixels stay. */
    Rect update;
    check(ValidateRect(h, 0) && !GetUpdateRect(h, &update, 0), 3637);
    check(EnableMenuItem(menu, 100, 1) != 0xffffffffu && DrawMenuBar(h), 3638);
    check(!GetUpdateRect(h, &update, 0), 3639);
    /* An explicit new row must also update an already attached menu. */
    check(AppendMenuA(menu, 0x40, 106, "Last row") && DrawMenuBar(h), 3617);
    check(GetClientRect(h, &extra) && equals(&extra, 0, 0, 692, 194), 3618);
    /* The bar grew and the client area moved, but the window is hidden:
     * RedrawWindow gives up, nothing becomes invalid. */
    check(!GetUpdateRect(h, &update, 0), 3640);
    origin(h, 44, 112, 3619);
    check(SetMenu(h, 0) && !GetMenu(h) && DrawMenuBar(h), 3620);
    client(h, 692, 233, 3621);
    origin(h, 44, 73, 3622);
    check(DestroyMenu(menu) && DestroyWindow(h), 3623);

    menu = CreateMenu();
    check(menu && AppendMenuA(menu, 0, 200, "Menu at creation"), 3624);
    h = window(0x80, CAPTION_FRAME, 40, 50, 300, 200, 0, menu);
    check(h && GetMenu(h) == menu, 3625);
    client(h, 292, 155, 3626);
    origin(h, 44, 91, 3627);
    check(SetMenu(h, 0) && DestroyMenu(menu) && DestroyWindow(h), 3628);
}

void run_nonclient_test(U module)
{
    struct
    {
        U style;
        U(__stdcall *proc)(U, U, U, U);
        int ce, we;
        U instance, icon, cursor, background;
        const char *menu, *name;
    } klass = {0, procedure, 0, 0, module, 0, 0, 16, 0, class_name};
    instance = module;
    check(RegisterClassA(&klass), 3550);
    adjustment();

    U popup = window(0, 0x80000000u, 20, 30, 300, 200, 0, 0);
    U normal = window(0, CAPTION_FRAME, 100, 80, 300, 200, 0, 0);
    U tool = window(0x80, CAPTION_FRAME, 420, 100, 300, 200, 0, 0);
    check(popup && normal && tool, 3560);
    client(popup, 300, 200, 3561);
    outer(popup, 20, 30, 320, 230, 3562);
    origin(popup, 20, 30, 3563);
    client(normal, 292, 173, 3564);
    outer(normal, 100, 80, 400, 280, 3565);
    origin(normal, 104, 103, 3566);
    client(tool, 292, 175, 3567);
    origin(tool, 424, 121, 3568);
    check((GetWindowLongA(normal, -20) & 0x100) && (GetWindowLongA(tool, -20) & 0x180) == 0x180, 3569);

    U child = window(0x200, CHILD_FRAME_SCROLL, 11, 13, 200, 100, normal, 42);
    check(child != 0, 3570);
    client(child, 177, 77, 3571);
    outer(child, 115, 116, 315, 216, 3572);
    origin(child, 118, 119, 3573);
    Point p = {0, 0};
    check(MapWindowPoints(child, normal, &p, 1) == delta(14, 16) && p.x == 14 && p.y == 16, 3574);
    check(SetWindowPos(normal, 0, 120, 90, 0, 0, 0x15), 3575); /* move only */
    outer(child, 135, 126, 335, 226, 3576);
    origin(child, 138, 129, 3577);
    check(DestroyWindow(child), 3578);

    /* Take a DC, then remove every pending paint. The window is hidden, so
     * the frame change leaves no update region: USER gives up at
     * for a window without WS_VISIBLE. */
    U dc = GetDC(normal);
    check(dc && ReleaseDC(normal, dc) && ValidateRect(normal, 0) && !GetUpdateRect(normal, 0, 0), 3637);
    /* Win98 adds WS_CLIPSIBLINGS when creating an overlapped window. */
    check(SetWindowLongA(normal, -16, 0x80000000u) == (CAPTION_FRAME | 0x04000000u) &&
              SetWindowPos(normal, 0, 0, 0, 0, 0, FRAME_CHANGED),
          3579);
    client(normal, 300, 200, 3580);
    origin(normal, 120, 90, 3581);
    Rect update;
    check(!GetUpdateRect(normal, &update, 0) && equals(&update, 0, 0, 0, 0), 3638);
    Paint paint = {0};
    dc = BeginPaint(normal, &paint);
    check(dc && paint.dc == dc && equals(&paint.rect, 0, 0, 0, 0), 3639);
    /* The window is hidden: its DC is a DC on the screen with an empty visible
     * region, so no pixel of it answers (Win98 GetPixel gives CLR_INVALID
     * outside the clip); the old per-window canvas answered inside it. */
    check(GetPixel(dc, 299, 199) == 0xffffffffu && GetPixel(dc, 300, 199) == 0xffffffffu, 3640);
    check(EndPaint(normal, &paint) && !GetUpdateRect(normal, 0, 0), 3641);
    check(!(GetWindowLongA(normal, -20) & 0x100), 3582);
    check(SetWindowLongA(normal, -16, CAPTION_FRAME) == 0x80000000u, 3583);
    SetWindowLongA(normal, -20, 0x200);
    check(SetWindowPos(normal, 0, 0, 0, 0, 0, FRAME_CHANGED) &&
              (GetWindowLongA(normal, -20) & 0x300) == 0x300,
          3584);
    client(normal, 288, 169, 3585);
    origin(normal, 126, 115, 3586);
    /* USER writes the style word and nothing else; the frame follows at
       the next SetWindowPos with SWP_FRAMECHANGED, as three lines above. Without that call the client rectangle stays what it was. */
    SetWindowLongA(normal, -20, 0x80);
    client(normal, 288, 169, 3648);
    check(SetWindowPos(normal, 0, 0, 0, 0, 0, FRAME_CHANGED), 3649);
    client(normal, 292, 175, 3587);
    origin(normal, 124, 111, 3588);

    /* The new border hit path sends WM_NCLBUTTONDOWN to native EDIT controls.
     * Its default handling must not enter the unsupported-edit-message trap. */
    U edit =
        CreateWindowExA(0x200, "EDIT", "Border input", 0x40800000, 11, 13, 80, 40, popup, 77, instance, 0);
    check(edit != 0, 3642);
    client(edit, 74, 34, 3643);
    origin(edit, 34, 46, 3644);
    check(SendMessageA(edit, 0xa1, 18, delta(31, 43)) == 0, 3645);
    client(edit, 74, 34, 3646);
    check(DestroyWindow(edit), 3647);
    check(DestroyWindow(normal) && DestroyWindow(tool) && DestroyWindow(popup), 3589);

    tiny_windows();
    menu_geometry();

    U minimized = window(0, CAPTION_FRAME | 0x20000000u, 60, 70, 300, 200, 0, 0);
    check(minimized != 0, 3629);
    client(minimized, 0, 0, 3630);
    /* Minimized top-level windows live at the iconic parking position;
     * restoring uses the saved normal rectangle, not a style-bit edit. */
    outer(minimized, 3000, 3000, 3000 + GetSystemMetrics(57), 3000 + GetSystemMetrics(58), 3631);
    origin(minimized, 3000, 3000, 3632);
    ShowWindow(minimized, 9);
    check(SetWindowPos(minimized, 0, 0, 0, 0, 0, FRAME_CHANGED), 3633);
    client(minimized, 292, 173, 3634);
    origin(minimized, 64, 93, 3635);
    check(DestroyWindow(minimized), 3636);
}
