/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Transparent statics under a dialog-like parent, the way Windows Installer
   (msihnd.dll 1.20) drives its Text controls: every control is subclassed,
   the parent answers WM_CTLCOLORSTATIC with a hollow brush and TRANSPARENT
   background mode, and its class brush is set later with SetClassLong. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U style, proc; int ce, we; U instance, icon, cursor, background; const char *menu, *name; } Class;
typedef struct { int left, top, right, bottom; } Rect;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } Msg;
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API void OutputDebugStringA(const char *);
API U RegisterClassA(const Class *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U CallWindowProcA(U, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SetWindowLongA(U, int, U);
API U SetFocus(U);
API U GetWindowLongA(U, int);
API U SetClassLongA(U, int, U);
API U SetWindowTextA(U, const char *);
API U ShowWindow(U, int);
API U InvalidateRect(U, const Rect *, U);
API U UpdateWindow(U);
API U PeekMessageA(Msg *, U, U, U, U);
API U DispatchMessageA(const Msg *);
API U GetDC(U);
API U ReleaseDC(U, U);
API U GetSysColor(int);
API U GetPixel(U, int, int);
API U SetBkMode(U, int);
API U GetStockObject(int);
API U CreateSolidBrush(U);
API U SendMessageA(U, U, U, U);
API U IsWindowVisible(U);
API U GetWindowTextA(U, char *, int);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U TextOutA(U, int, int, const char *, int);
API U FillRect(U, const Rect *, U);
static U original, paints;
static U __stdcall subclass(U h, U m, U w, U l)
{
    if (m == 0xf) ++paints;
    return CallWindowProcA(original, h, m, w, l);
}
/* Windows Installer: a transparent Text control paints its text itself,
   in TRANSPARENT mode and without filling its background. */
static U text_original;
static U __stdcall text_proc(U h, U m, U w, U l)
{
    if (m == 0xf) {
        U paint[16];
        char text[64];
        U dc = BeginPaint(h, paint);
        int n = (int)GetWindowTextA(h, text, sizeof text);
        SetBkMode(dc, 1);
        TextOutA(dc, 0, 0, text, n);
        EndPaint(h, paint);
        return 0;
    }
    return CallWindowProcA(text_original, h, m, w, l);
}
/* A STATIC subclass can paint content independent of its window text,
   as Half-Life's MFC explanation control does. */
static U custom_original, custom_paints;
static U __stdcall custom_proc(U h, U m, U w, U l)
{
    if (m == 0xf) {
        U paint[16];
        U dc = BeginPaint(h, paint);
        Rect rect = {0, 0, 260, 16};
        static U brush;
        if (!brush) brush = CreateSolidBrush(0xff);
        FillRect(dc, &rect, brush);
        EndPaint(h, paint);
        ++custom_paints;
        return 0;
    }
    return CallWindowProcA(custom_original, h, m, w, l);
}
/* How Windows Installer puts new ActionText into such a
   control. WM_SETREDRAW(FALSE) takes WS_VISIBLE off it, so the
   STATIC does not draw at WM_SETTEXT and ShowWindow shows it again. */
static void set_action_text(U h, const char *text)
{
    SendMessageA(h, 0xb, 0, 0);
    SetWindowTextA(h, text);
    ShowWindow(h, 5);
    SendMessageA(h, 0xb, 1, 0);
    InvalidateRect(h, 0, 1);
}
static U __stdcall parent_proc(U h, U m, U w, U l)
{
    /* Use a hollow brush only for a transparent control. */
    if (m == 0x138 && (GetWindowLongA(l, -20) & 0x20)) { SetBkMode(w, 1); return GetStockObject(5); }
    return DefWindowProcA(h, m, w, l);
}
static void pump(void)
{
    Msg msg;
    for (int round = 0; round < 40; ++round) {
        while (PeekMessageA(&msg, 0, 0, 0, 1)) DispatchMessageA(&msg);
        Sleep(10);
    }
}
/* Pixels in the screen rectangle that are not the dialog face. */
static U foreign(int left, int top, int right, int bottom)
{
    U dc = GetDC(0), face = GetSysColor(15), count = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (GetPixel(dc, x, y) != face) ++count;
    ReleaseDC(0, dc);
    return count;
}
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    Class parent = {0, (U)parent_proc, 0, 0, instance, 0, 0, 0, 0, "MsiLikeDialog"};
    if (!RegisterClassA(&parent)) ExitProcess(1);
    /* WS_POPUP | WS_VISIBLE, no WS_CLIPCHILDREN, like MsiDialogCloseClass. */
    U p = CreateWindowExA(0, parent.name, "", 0x90000000, 100, 100, 300, 200, 0, 0, instance, 0);
    if (!p) ExitProcess(2);
    SetClassLongA(p, -10, CreateSolidBrush(GetSysColor(15)));
    InvalidateRect(p, 0, 1);
    /* WS_CHILD | WS_VISIBLE | SS_LEFT, WS_EX_TRANSPARENT. */
    U a = CreateWindowExA(0x20, "STATIC", "", 0x50000000, 20, 20, 200, 16, p, 1, instance, 0);
    U b = CreateWindowExA(0x20, "STATIC", "", 0x50000000, 20, 60, 200, 16, p, 2, instance, 0);
    if (!a || !b) ExitProcess(3);
    original = SetWindowLongA(a, -4, (U)subclass);
    SetWindowLongA(b, -4, (U)subclass);
    pump();
    if (foreign(100, 100, 400, 300)) ExitProcess(4);
    /* The hidden control's old text: hiding a child exposes its parent. */
    SetWindowTextA(b, "WWWWWWWWWWWWWWWW");
    pump();
    if (!foreign(120, 160, 320, 176)) ExitProcess(5);
    ShowWindow(b, 0);
    pump();
    if (foreign(120, 160, 320, 176)) ExitProcess(6);
    /* A paint of a subclassed control goes through its window procedure. */
    paints = 0;
    SetWindowTextA(a, "MMMMMMMMMMMMMMMM");
    ShowWindow(a, 5);
    InvalidateRect(a, 0, 1);
    pump();
    if (!paints) ExitProcess(7);
    /* A subclassed STATIC that holds EDIT boxes, as the MaskedEdit control
       does: the boxes stay on top of their parent's paint. */
    U holder = CreateWindowExA(0x10000, "STATIC", "", 0x50000000, 20, 100, 200, 20, p, 3, instance, 0);
    if (!holder) ExitProcess(8);
    SetWindowLongA(holder, -4, (U)subclass);
    U edit = CreateWindowExA(0, "EDIT", "", 0x50810080, 10, 1, 80, 18, holder, 4, instance, 0);
    if (!edit) ExitProcess(9);
    /* The caret of the focused box keeps new frames coming. */
    SetFocus(edit);
    pump();
    U dc = GetDC(0), window = GetSysColor(5);
    U inside = GetPixel(dc, 100 + 20 + 10 + 40, 100 + 100 + 1 + 9);
    ReleaseDC(0, dc);
    if (inside != window) ExitProcess(10);
    InvalidateRect(holder, 0, 1);
    pump();
    /* New frames for other reasons: the other text keeps changing. */
    for (int round = 0; round < 10; ++round) {
        SetWindowTextA(a, round & 1 ? "MMMM" : "WWWW");
        pump();
    }
    dc = GetDC(0);
    inside = GetPixel(dc, 100 + 20 + 10 + 40, 100 + 100 + 1 + 9);
    ReleaseDC(0, dc);
    if (inside != window) ExitProcess(11);
    /* Windows Installer's ActionText: every new text replaces the
       old one; a narrow text after a wide one leaves only the dialog face to
       its right. */
    U c = CreateWindowExA(0x20, "STATIC", "", 0x50000000, 20, 140, 260, 16, p, 5, instance, 0);
    if (!c) ExitProcess(12);
    text_original = SetWindowLongA(c, -4, (U)text_proc);
    pump();
    SendMessageA(c, 0xb, 0, 0);
    if (IsWindowVisible(c) || (GetWindowLongA(c, -16) & 0x10000000)) ExitProcess(13);
    if (ShowWindow(c, 5)) ExitProcess(14);
    if (!IsWindowVisible(c)) ExitProcess(15);
    SendMessageA(c, 0xb, 1, 0);
    if (!IsWindowVisible(c)) ExitProcess(16);
    static const char *const texts[] = {"WWWWWWWWWWWWWWWWWWWW", "MMMMMMMMMMMM", "WWWWWWWWWWWWWWWWWWWW", "i"};
    for (int round = 0; round < 4; ++round) {
        set_action_text(c, texts[round]);
        pump();
    }
    /* "i" covers a few pixels at the left; everything right of it is face. */
    if (foreign(100 + 20 + 12, 100 + 140, 100 + 20 + 260, 100 + 140 + 16)) ExitProcess(17);
    if (!foreign(100 + 20, 100 + 140, 100 + 20 + 12, 100 + 140 + 16)) ExitProcess(18);
    U d = CreateWindowExA(0, "STATIC", "", 0x50000000, 20, 180, 260, 16, p, 6, instance, 0);
    if (!d) ExitProcess(19);
    custom_original = SetWindowLongA(d, -4, (U)custom_proc);
    InvalidateRect(d, 0, 1);
    /* Half-Life sends WM_PAINT directly after moving its subclassed STATIC.
       This must consume the stock control's pending paint too. */
    SendMessageA(d, 0xf, 0, 0);
    pump();
    U drawn = custom_paints;
    if (!drawn) ExitProcess(20);
    for (int round = 0; round < 4; ++round) {
        SetWindowTextA(a, round & 1 ? "MMMM" : "WWWW");
        pump();
        dc = GetDC(0);
        U pixel = GetPixel(dc, 130, 285);
        ReleaseDC(0, dc);
        if (pixel != 0xff || custom_paints != drawn) ExitProcess(21);
    }
    InvalidateRect(d, 0, 1);
    pump();
    if (custom_paints <= drawn) ExitProcess(22);
    OutputDebugStringA("TRANSPARENT STATIC FIXTURE DONE\n");
    ExitProcess(0);
}
