/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A catalogue of USER32 and GDI32 calls, each made by a worker thread while
 * the main thread holds the primary surface of the original DDRAW.DLL
 * locked. The port lets only the calls that touch the screen's pixels wait
 * at their gate (src/x86_screen_lock.cpp, ScreenPixelRule); behind that list
 * GDI reports every access to the locked pixels that did not wait
 * ("SCREEN LOCK MISS", Application::ScreenTouched). tools/screen_pixel_exports.py
 * runs this fixture twice: with NEO98_SCREEN_RULES=0 the misses name every
 * call of the catalogue that touches the screen, which is the list derived
 * from the code; with the list in force there must be none.
 *
 * Every call is resolved with GetProcAddress and made through call_any,
 * which restores the stack itself, so one table serves every signature. A
 * line "SPX <index> <name>" before each call tells the tool which catalogue
 * entry a miss belongs to. Nothing here waits for anything but the lock. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U CreateEventA(U, U, U, const char *);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U GetModuleHandleA(const char *);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);

static U user32, gdi32, msimg32;
static U proc(U module, const char *name) { return module ? GetProcAddress(module, name) : 0; }

/* stdcall and cdecl alike: the stack comes back from esi. */
static U call_any(U fn, const U *args, U count)
{
    U result;
    __asm__ volatile("movl %%esp, %%esi\n\t"
                     "1: testl %%ecx, %%ecx\n\t"
                     "jz 2f\n\t"
                     "decl %%ecx\n\t"
                     "pushl (%%edi,%%ecx,4)\n\t"
                     "jmp 1b\n\t"
                     "2: call *%%ebx\n\t"
                     "movl %%esi, %%esp\n\t"
                     : "=a"(result), "+c"(count)
                     : "b"(fn), "D"(args)
                     : "edx", "esi", "memory", "cc");
    return result;
}

static U call_user(const char *name, U a, U b, U c, U d)
{
    const U args[4] = {a, b, c, d};
    U fn = proc(user32, name);
    return fn ? call_any(fn, args, 4) : 0;
}
static U call_user_n(const char *name, const U *args, U count)
{
    U fn = proc(user32, name);
    return fn ? call_any(fn, args, count) : 0;
}
static U call_gdi(const char *name, U a, U b, U c, U d, U e)
{
    const U args[5] = {a, b, c, d, e};
    U fn = proc(gdi32, name);
    return fn ? call_any(fn, args, 5) : 0;
}

static char line[160];
static void say(const char *prefix, U index, const char *text)
{
    U at = 0, i = 0;
    while (prefix[i]) line[at++] = prefix[i++];
    char digits[12];
    U n = 0;
    do { digits[n++] = (char)('0' + index % 10); index /= 10; } while (index);
    while (n) line[at++] = digits[--n];
    line[at++] = ' ';
    for (i = 0; text[i] && at < sizeof line - 1; ++i) line[at++] = text[i];
    line[at] = 0;
    OutputDebugStringA(line);
}

/* Symbolic arguments, resolved by the worker. Everything else is literal. */
enum {
    Tag = 0xa5000000u,
    WDC = Tag | 1,   /* the worker window's DC: the screen */
    SDC,             /* GetDC(0) */
    MDC,             /* a memory DC with a 32x32 bitmap */
    WND,             /* the worker window: WS_OVERLAPPEDWINDOW, scroll bars, menu */
    DWP,             /* a window whose class procedure is DefWindowProcA itself */
    DDC,             /* its DC */
    STC,             /* a STATIC child of WND */
    BTN,             /* a BS_AUTOCHECKBOX child */
    EDT,             /* an EDIT child */
    LBX,             /* a LISTBOX child */
    ANI,             /* a window for AnimateWindow */
    MNU,             /* the menu of WND */
    RC1, RC2,        /* RECT pointers */
    PTS,             /* POINT array */
    CNT,             /* DWORD counts {4, 4} */
    TXT, WTX,        /* "ab", L"ab" */
    TYP,             /* PolyDraw types */
    BRU,             /* a stock brush */
    RGN,             /* a region */
    ICO,             /* IDI_APPLICATION */
    BMI, BIT,        /* a 16x16x32 BITMAPINFO and its bits */
    PTX, PTW,        /* POLYTEXTA, POLYTEXTW */
    DWPROC,          /* the address of DefWindowProcA */
    FWI,             /* FLASHWINFO for WND */
    SCI,             /* SCROLLINFO */
    VTX,             /* TRIVERTEX pair for GradientFill */
    GRC,             /* GRADIENT_RECT */
    PRS              /* PAINTSTRUCT */
};
enum { PrepNone, PrepPath, PrepMoveTo, PrepInvalidate, PrepInvalidateStatic, PrepCaret };
enum { DllUser, DllGdi, DllImg };
struct Entry { unsigned char dll, prep; const char *name; U argc; U args[13]; };

static const struct Entry catalogue[] = {
    /* GDI32: drawing on a DC of the screen. */
    {DllGdi, 0, "Arc", 9, {WDC, 2, 2, 20, 20, 2, 2, 20, 20}},
    {DllGdi, 0, "ArcTo", 9, {WDC, 2, 2, 20, 20, 2, 2, 20, 20}},
    {DllGdi, 0, "AngleArc", 6, {WDC, 10, 10, 5, 0, 0x42b40000u}},
    {DllGdi, 0, "Chord", 9, {WDC, 2, 2, 20, 20, 2, 2, 20, 20}},
    {DllGdi, 0, "Ellipse", 5, {WDC, 2, 2, 20, 20}},
    {DllGdi, 0, "Pie", 9, {WDC, 2, 2, 20, 20, 2, 2, 20, 20}},
    {DllGdi, 0, "Rectangle", 5, {WDC, 2, 2, 20, 20}},
    {DllGdi, 0, "RoundRect", 7, {WDC, 2, 2, 20, 20, 4, 4}},
    {DllGdi, 0, "Polygon", 3, {WDC, PTS, 4}},
    {DllGdi, 0, "Polyline", 3, {WDC, PTS, 4}},
    {DllGdi, 0, "PolylineTo", 3, {WDC, PTS, 4}},
    {DllGdi, 0, "PolyPolygon", 4, {WDC, PTS, CNT, 2}},
    {DllGdi, 0, "PolyPolyline", 4, {WDC, PTS, CNT, 2}},
    {DllGdi, 0, "PolyBezier", 3, {WDC, PTS, 4}},
    {DllGdi, 0, "PolyBezierTo", 3, {WDC, PTS, 3}},
    {DllGdi, 0, "PolyDraw", 4, {WDC, PTS, TYP, 4}},
    {DllGdi, PrepMoveTo, "LineTo", 3, {WDC, 20, 20}},
    {DllGdi, 0, "PatBlt", 6, {WDC, 2, 2, 16, 16, 0x42}},
    {DllGdi, 0, "BitBlt", 9, {WDC, 2, 2, 16, 16, MDC, 0, 0, 0x00cc0020}},
    {DllGdi, 0, "BitBlt", 9, {MDC, 0, 0, 16, 16, SDC, 2, 2, 0x00cc0020}},
    {DllGdi, 0, "StretchBlt", 11, {WDC, 2, 2, 16, 16, MDC, 0, 0, 8, 8, 0x00cc0020}},
    {DllGdi, 0, "StretchBlt", 11, {MDC, 0, 0, 16, 16, SDC, 2, 2, 8, 8, 0x00cc0020}},
    {DllGdi, 0, "MaskBlt", 12, {WDC, 2, 2, 16, 16, MDC, 0, 0, 0, 0, 0, 0x00cc0020}},
    {DllGdi, 0, "PlgBlt", 10, {WDC, PTS, MDC, 0, 0, 16, 16, 0, 0, 0}},
    {DllGdi, 0, "StretchDIBits", 13, {WDC, 2, 2, 16, 16, 0, 0, 16, 16, BIT, BMI, 0, 0x00cc0020}},
    {DllGdi, 0, "SetDIBitsToDevice", 12, {WDC, 2, 2, 16, 16, 0, 0, 0, 16, BIT, BMI, 0}},
    {DllGdi, 0, "SetPixel", 4, {WDC, 3, 3, 0}},
    {DllGdi, 0, "SetPixelV", 4, {WDC, 3, 3, 0}},
    {DllGdi, 0, "GetPixel", 3, {SDC, 3, 3}},
    {DllGdi, 0, "TextOutA", 5, {WDC, 2, 2, TXT, 2}},
    {DllGdi, 0, "TextOutW", 5, {WDC, 2, 2, WTX, 2}},
    {DllGdi, 0, "ExtTextOutA", 8, {WDC, 2, 2, 2, RC1, TXT, 2, 0}},
    {DllGdi, 0, "ExtTextOutW", 8, {WDC, 2, 2, 2, RC1, WTX, 2, 0}},
    {DllGdi, 0, "PolyTextOutA", 3, {WDC, PTX, 1}},
    {DllGdi, 0, "PolyTextOutW", 3, {WDC, PTW, 1}},
    {DllGdi, 0, "FillRgn", 3, {WDC, RGN, BRU}},
    {DllGdi, 0, "FrameRgn", 5, {WDC, RGN, BRU, 1, 1}},
    {DllGdi, 0, "InvertRgn", 2, {WDC, RGN}},
    {DllGdi, 0, "PaintRgn", 2, {WDC, RGN}},
    {DllGdi, 0, "FloodFill", 4, {WDC, 5, 5, 0x123456}},
    {DllGdi, 0, "ExtFloodFill", 5, {WDC, 5, 5, 0x123456, 0}},
    {DllGdi, PrepPath, "FillPath", 1, {WDC}},
    {DllGdi, PrepPath, "StrokePath", 1, {WDC}},
    {DllGdi, PrepPath, "StrokeAndFillPath", 1, {WDC}},
    {DllGdi, 0, "UpdateColors", 1, {WDC}},
    /* GDI32: calls on a DC of the screen that draw nothing. */
    {DllGdi, 0, "GetTextColor", 1, {WDC}},
    {DllGdi, 0, "SetTextColor", 2, {WDC, 0x00ff00}},
    {DllGdi, 0, "GetDeviceCaps", 2, {WDC, 12}},
    {DllGdi, 0, "GetClipBox", 2, {WDC, RC2}},
    {DllGdi, 0, "MoveToEx", 4, {WDC, 2, 2, 0}},
    {DllGdi, 0, "SelectObject", 2, {WDC, BRU}},
    {DllGdi, 0, "GetTextExtentPoint32A", 4, {WDC, TXT, 2, RC2}},
    /* MSIMG32 (Win98): blits of their own DLL. */
    {DllImg, 0, "TransparentBlt", 11, {WDC, 2, 2, 16, 16, MDC, 0, 0, 16, 16, 0x00ff00ff}},
    {DllImg, 0, "AlphaBlend", 11, {WDC, 2, 2, 16, 16, MDC, 0, 0, 16, 16, 0x00ff0000}},
    {DllImg, 0, "GradientFill", 6, {WDC, VTX, 2, GRC, 1, 0}},
    /* USER32: drawing on a DC of the screen. */
    {DllUser, 0, "FillRect", 3, {WDC, RC1, BRU}},
    {DllUser, 0, "FrameRect", 3, {WDC, RC1, BRU}},
    {DllUser, 0, "InvertRect", 2, {WDC, RC1}},
    {DllUser, 0, "DrawTextA", 5, {WDC, TXT, 2, RC1, 0}},
    {DllUser, 0, "DrawTextW", 5, {WDC, WTX, 2, RC1, 0}},
    {DllUser, 0, "DrawTextExA", 6, {WDC, TXT, 2, RC1, 0, 0}},
    {DllUser, 0, "DrawTextExW", 6, {WDC, WTX, 2, RC1, 0, 0}},
    {DllUser, 0, "DrawEdge", 4, {WDC, RC1, 5, 15}},
    {DllUser, 0, "DrawFocusRect", 2, {WDC, RC1}},
    {DllUser, 0, "DrawIcon", 4, {WDC, 2, 2, ICO}},
    {DllUser, 0, "DrawIconEx", 9, {WDC, 2, 2, ICO, 16, 16, 0, 0, 3}},
    {DllUser, 0, "DrawFrameControl", 4, {WDC, RC1, 4, 0x10}},
    {DllUser, 0, "DrawStateA", 10, {WDC, 0, 0, TXT, 2, 2, 2, 16, 16, 1}},
    {DllUser, 0, "DrawStateW", 10, {WDC, 0, 0, WTX, 2, 2, 2, 16, 16, 1}},
    {DllUser, 0, "GrayStringA", 9, {WDC, BRU, 0, TXT, 2, 2, 2, 16, 16}},
    {DllUser, 0, "GrayStringW", 9, {WDC, BRU, 0, WTX, 2, 2, 2, 16, 16}},
    {DllUser, 0, "TabbedTextOutA", 8, {WDC, 2, 2, TXT, 2, 0, 0, 0}},
    {DllUser, 0, "TabbedTextOutW", 8, {WDC, 2, 2, WTX, 2, 0, 0, 0}},
    {DllUser, 0, "PaintDesktop", 1, {WDC}},
    {DllUser, 0, "ScrollDC", 7, {WDC, 1, 1, RC1, RC1, 0, 0}},
    {DllUser, 0, "DrawCaption", 4, {WND, WDC, RC1, 9}},
    /* USER32: the pixels of a window. */
    {DllUser, 0, "DrawAnimatedRects", 4, {WND, 3, RC1, RC2}},
    {DllUser, 0, "ScrollWindow", 5, {WND, 1, 1, 0, 0}},
    {DllUser, 0, "ScrollWindowEx", 8, {WND, 1, 1, 0, 0, 0, 0, 0}},
    {DllUser, 0, "DrawMenuBar", 1, {WND}},
    {DllUser, 0, "FlashWindow", 2, {WND, 1}},
    {DllUser, 0, "FlashWindowEx", 1, {FWI}},
    {DllUser, 0, "DefWindowProcA", 4, {WND, 0x0085, 1, 0}},
    {DllUser, 0, "DefWindowProcA", 4, {WND, 0x0014, WDC, 0}},
    {DllUser, 0, "DefWindowProcA", 4, {WND, 0x0086, 1, 0}},
    {DllUser, 0, "DefWindowProcA", 4, {WND, 0x000c, 0, TXT}},
    {DllUser, 0, "DefWindowProcA", 4, {WND, 0x0317, WDC, 0x0e}},
    {DllUser, 0, "DefWindowProcW", 4, {WND, 0x0085, 1, 0}},
    {DllUser, 0, "CallWindowProcA", 5, {DWPROC, DWP, 0x0085, 1, 0}},
    {DllUser, 0, "SendMessageA", 4, {DWP, 0x0085, 1, 0}},
    {DllUser, 0, "SendMessageA", 4, {DWP, 0x0014, DDC, 0}},
    {DllUser, 0, "SendMessageA", 4, {DWP, 0x0086, 0, 0}},
    {DllUser, 0, "SetWindowTextA", 2, {WND, TXT}},
    {DllUser, 0, "SetWindowTextA", 2, {DWP, TXT}},
    {DllUser, PrepInvalidate, "UpdateWindow", 1, {DWP}},
    {DllUser, PrepInvalidate, "RedrawWindow", 4, {DWP, 0, 0, 0x0785}},
    {DllUser, PrepInvalidate, "RedrawWindow", 4, {WND, 0, 0, 0x0785}},
    {DllUser, PrepInvalidate, "BeginPaint", 2, {DWP, PRS}},
    {DllUser, 0, "EndPaint", 2, {DWP, PRS}},
    {DllUser, 0, "SetScrollPos", 4, {WND, 1, 40, 1}},
    {DllUser, 0, "SetScrollRange", 5, {WND, 1, 0, 200, 1}},
    {DllUser, 0, "SetScrollInfo", 4, {WND, 0, SCI, 1}},
    {DllUser, 0, "ShowScrollBar", 3, {WND, 0, 1}},
    {DllUser, 0, "EnableScrollBar", 3, {WND, 1, 3}},
    {DllUser, 0, "HiliteMenuItem", 4, {WND, MNU, 0, 0x0480}},
    {DllUser, 0, "SetMenu", 2, {WND, MNU}},
    {DllUser, 0, "SetActiveWindow", 1, {DWP}},
    {DllUser, 0, "SetForegroundWindow", 1, {WND}},
    {DllUser, 0, "EnableWindow", 2, {DWP, 0}},
    {DllUser, 0, "EnableWindow", 2, {DWP, 1}},
    {DllUser, 0, "SetWindowRgn", 3, {DWP, 0, 1}},
    {DllUser, PrepCaret, "ShowCaret", 1, {WND}},
    /* USER32: the native controls. */
    {DllUser, 0, "SetWindowTextA", 2, {STC, TXT}},
    {DllUser, PrepInvalidateStatic, "UpdateWindow", 1, {STC}},
    {DllUser, PrepInvalidateStatic, "UpdateWindow", 1, {WND}},
    {DllUser, PrepInvalidateStatic, "RedrawWindow", 4, {STC, 0, 0, 0x0185}},
    {DllUser, PrepInvalidateStatic, "RedrawWindow", 4, {WND, 0, 0, 0x0185}},
    {DllUser, 0, "SendMessageA", 4, {BTN, 0x00f1, 1, 0}},
    {DllUser, 0, "SendMessageA", 4, {BTN, 0x00f3, 1, 0}},
    {DllUser, 0, "EnableWindow", 2, {BTN, 0}},
    {DllUser, 0, "CheckDlgButton", 3, {WND, 11, 0}},
    {DllUser, 0, "SendMessageA", 4, {EDT, 0x00c2, 0, TXT}},
    {DllUser, 0, "SendMessageA", 4, {EDT, 0x000c, 0, TXT}},
    {DllUser, 0, "SetFocus", 1, {EDT}},
    {DllUser, 0, "SendMessageA", 4, {LBX, 0x0180, 0, TXT}},
    {DllUser, 0, "SendMessageA", 4, {LBX, 0x0186, 0, 0}},
    /* USER32: calls that draw nothing. */
    {DllUser, 0, "SetWindowPos", 7, {WND, 0, 12, 12, 260, 200, 0x14}},
    {DllUser, 0, "InvalidateRect", 3, {WND, 0, 1}},
    {DllUser, 0, "GetWindowRect", 2, {WND, RC2}},
    {DllUser, 0, "GetSysColor", 1, {5}},
    {DllUser, 0, "ShowWindow", 2, {ANI, 5}},
    {DllUser, 0, "AnimateWindow", 3, {ANI, 20, 0x00010004}},
};

static U resolve(U value, U worker_dc, U screen_dc, U memory_dc);
static U window, def_window, def_dc, static_window, button, edit, listbox, animated, menu;
static U brush, region, icon, def_proc;
static int rect1[4] = {2, 2, 20, 20}, rect2[4] = {4, 4, 30, 30};
static int points[16] = {2, 2, 20, 2, 20, 20, 2, 20, 5, 5, 10, 10, 15, 5, 2, 12};
static U counts[2] = {4, 4};
static const char text[] = "ab";
static const unsigned short wide[] = {'a', 'b', 0};
static unsigned char types[4] = {6, 2, 2, 2};
static U info[10] = {40, 16, 16, 1 | (32u << 16), 0, 0, 0, 0, 0, 0};
static U bits[16 * 16];
static U polytext[10], polytextw[10];
static U flash[5];
static U scroll[7] = {28, 0x17, 0, 200, 20, 60, 0};
static U vertices[8] = {0, 0, 0xff000000u, 0, 20, 20, 0xff00u, 0xff00ff00u};
static U gradient[2] = {0, 1};
static U paint[16];

static U resolve(U value, U worker_dc, U screen_dc, U memory_dc)
{
    switch (value)
    {
    case WDC: return worker_dc;
    case SDC: return screen_dc;
    case MDC: return memory_dc;
    case WND: return window;
    case DWP: return def_window;
    case DDC: return def_dc;
    case STC: return static_window;
    case BTN: return button;
    case EDT: return edit;
    case LBX: return listbox;
    case ANI: return animated;
    case MNU: return menu;
    case RC1: return (U)rect1;
    case RC2: return (U)rect2;
    case PTS: return (U)points;
    case CNT: return (U)counts;
    case TXT: return (U)text;
    case WTX: return (U)wide;
    case TYP: return (U)types;
    case BRU: return brush;
    case RGN: return region;
    case ICO: return icon;
    case BMI: return (U)info;
    case BIT: return (U)bits;
    case PTX: return (U)polytext;
    case PTW: return (U)polytextw;
    case DWPROC: return def_proc;
    case FWI: return (U)flash;
    case SCI: return (U)scroll;
    case VTX: return (U)vertices;
    case GRC: return (U)gradient;
    case PRS: return (U)paint;
    default: return value;
    }
}

static U go_event, done_event, ready_event;
static volatile U current = ~0u;

static U __stdcall worker_procedure(U hwnd, U message, U wp, U lp)
{
    const U args[4] = {hwnd, message, wp, lp};
    return call_user_n("DefWindowProcA", args, 4);
}

static U create_window(const char *cls, const char *title, U style, int x, int y, int w, int h, U parent, U id)
{
    const U args[12] = {0, (U)cls, (U)title, style, (U)x, (U)y, (U)w, (U)h, parent, id, 0, 0};
    return call_user_n("CreateWindowExA", args, 12);
}

static U __stdcall worker(U unused)
{
    (void)unused;
    const U instance = GetModuleHandleA(0);
    struct { U style, proc; int extra, window; U instance, icon, cursor, brush; const char *menu, *name; } wc = {0};
    wc.proc = (U)worker_procedure;
    wc.instance = instance;
    wc.brush = 6; /* COLOR_WINDOW + 1 */
    wc.name = "SPXWorker";
    call_user("RegisterClassA", (U)&wc, 0, 0, 0);
    def_proc = proc(user32, "DefWindowProcA");
    wc.proc = def_proc;
    wc.name = "SPXPlain";
    call_user("RegisterClassA", (U)&wc, 0, 0, 0);
    menu = call_user("CreateMenu", 0, 0, 0, 0);
    {
        const U args[4] = {menu, 0, 100, (U)"File"};
        call_user_n("AppendMenuA", args, 4);
    }
    /* WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL */
    window = create_window("SPXWorker", "SPX worker", 0x10cf0000u | 0x00300000u, 300, 10, 260, 200, 0, 0);
    call_user("SetMenu", window, menu, 0, 0);
    def_window = create_window("SPXPlain", "SPX plain", 0x10cf0000u, 300, 230, 200, 120, 0, 0);
    animated = create_window("SPXPlain", "SPX animated", 0x10cf0000u, 520, 230, 120, 80, 0, 0);
    static_window = create_window("STATIC", "static", 0x50000000u, 4, 4, 60, 20, window, 10);
    button = create_window("BUTTON", "check", 0x50000003u, 4, 30, 80, 20, window, 11);
    edit = create_window("EDIT", "edit", 0x50800000u, 4, 56, 100, 20, window, 12);
    listbox = create_window("LISTBOX", "", 0x50800001u, 110, 4, 100, 60, window, 13);
    const U worker_dc = call_user("GetDC", window, 0, 0, 0);
    def_dc = call_user("GetDC", def_window, 0, 0, 0);
    const U screen_dc = call_user("GetDC", 0, 0, 0, 0);
    const U memory_dc = call_gdi("CreateCompatibleDC", screen_dc, 0, 0, 0, 0);
    call_gdi("SelectObject", memory_dc, call_gdi("CreateCompatibleBitmap", screen_dc, 32, 32, 0, 0), 0, 0, 0);
    brush = call_gdi("GetStockObject", 4, 0, 0, 0, 0);
    region = call_gdi("CreateRectRgn", 2, 2, 20, 20, 0);
    icon = call_user("LoadIconA", 0, 32512, 0, 0);
    polytext[0] = 2; polytext[1] = 2; polytext[2] = 2; polytext[3] = (U)text; polytext[4] = 0;
    polytext[5] = 2; polytext[6] = 2; polytext[7] = 20; polytext[8] = 20; polytext[9] = 0;
    for (U i = 0; i < 10; ++i) polytextw[i] = polytext[i];
    polytextw[3] = (U)wide;
    flash[0] = 20; flash[1] = window; flash[2] = 3; flash[3] = 1; flash[4] = 0;
    SetEvent(ready_event);
    for (;;)
    {
        WaitForSingleObject(go_event, 30000);
        const U index = current;
        if (index >= sizeof catalogue / sizeof catalogue[0]) break;
        const struct Entry *e = &catalogue[index];
        if (e->prep == PrepPath)
        {
            call_gdi("BeginPath", worker_dc, 0, 0, 0, 0);
            call_gdi("MoveToEx", worker_dc, 2, 2, 0, 0);
            call_gdi("LineTo", worker_dc, 20, 2, 0, 0);
            call_gdi("LineTo", worker_dc, 20, 20, 0, 0);
            call_gdi("LineTo", worker_dc, 2, 20, 0, 0);
            call_gdi("CloseFigure", worker_dc, 0, 0, 0, 0);
            call_gdi("EndPath", worker_dc, 0, 0, 0, 0);
        }
        else if (e->prep == PrepMoveTo) call_gdi("MoveToEx", worker_dc, 2, 2, 0, 0);
        else if (e->prep == PrepInvalidate) call_user("InvalidateRect", resolve(e->args[0], 0, 0, 0), 0, 1, 0);
        else if (e->prep == PrepInvalidateStatic) call_user("InvalidateRect", static_window, 0, 1, 0);
        else if (e->prep == PrepCaret)
        {
            call_user("CreateCaret", window, 0, 2, 12);
            call_user("SetCaretPos", 20, 20, 0, 0);
        }
        U args[13];
        for (U i = 0; i < e->argc; ++i) args[i] = resolve(e->args[i], worker_dc, screen_dc, memory_dc);
        const U module = e->dll == DllGdi ? gdi32 : e->dll == DllImg ? msimg32 : user32;
        const U fn = proc(module, e->name);
        say("SPX ", index, e->name);
        U answer = 0;
        if (fn) answer = call_any(fn, args, e->argc);
        else say("SPX absent ", index, e->name);
        say("SPX end ", index, e->name);
        say("SPX answer ", answer, e->name);
        if (e->prep == PrepCaret) call_user("DestroyCaret", 0, 0, 0, 0);
        SetEvent(done_event);
    }
    return 0;
}

void mainCRTStartup(void)
{
    user32 = LoadLibraryA("USER32.DLL");
    gdi32 = LoadLibraryA("GDI32.DLL");
    msimg32 = LoadLibraryA("MSIMG32.DLL");
    const U library = LoadLibraryA("DDRAW.DLL");
    const Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    void **ddraw = 0, **primary = 0;
    if (!create || create(0, (void **)&ddraw, 0) || !ddraw) { OutputDebugStringA("SPX no ddraw"); ExitProcess(1); }
    ((M3)((void **)*ddraw)[20])(ddraw, 0, 8); /* DDSCL_NORMAL */
    U desc[27] = {0};
    desc[0] = 108;
    desc[1] = 1;        /* DDSD_CAPS */
    desc[26] = 0x200;   /* DDSCAPS_PRIMARYSURFACE */
    if (((M4)((void **)*ddraw)[6])(ddraw, (U)desc, (U)&primary, 0) || !primary)
    {
        OutputDebugStringA("SPX no primary");
        ExitProcess(2);
    }
    go_event = CreateEventA(0, 0, 0, 0);
    done_event = CreateEventA(0, 0, 0, 0);
    ready_event = CreateEventA(0, 1, 0, 0);
    U id;
    CreateThread(0, 0, worker, 0, 0, &id);
    WaitForSingleObject(ready_event, 10000);
    const U total = sizeof catalogue / sizeof catalogue[0];
    for (U index = 0; index < total; ++index)
    {
        U lock[27] = {0};
        lock[0] = 108;
        const U result = ((M5)((void **)*primary)[25])(primary, 0, (U)lock, 1, 0); /* DDLOCK_WAIT */
        current = index;
        SetEvent(go_event);
        Sleep(20);
        if (!result) ((M2)((void **)*primary)[32])(primary, 0);
        if (WaitForSingleObject(done_event, 5000)) say("SPX stuck ", index, catalogue[index].name);
    }
    current = ~0u;
    SetEvent(go_event);
    OutputDebugStringA("SPX done");
    ExitProcess(0);
}
