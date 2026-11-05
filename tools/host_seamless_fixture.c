/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// The seamless view of the host (tools/test_seamless_host.py).
// Two popup windows on an otherwise empty desktop: A at 100,80, 300 by 200,
// red, with an elliptic window region, and B at 250,180, 200 by 150, blue, in
// front of it. The title of A tells the test what the guest saw:
// "screen WxH" at the start and after every WM_DISPLAYCHANGE
// "A down X,Y" WM_LBUTTONDOWN in A, client coordinates
// "A drag X,Y" WM_MOUSEMOVE in A with the left button down
// "A up X,Y" WM_LBUTTONUP in A
// "A wheel N,M" WM_MOUSEWHEEL in A: the wheel's delta and their count
// Keys, to whichever of the two has the focus:
// P 640x480 as a program mode (CDS_FULLSCREEN) -> "program WxH"
// D back to the desktop (ChangeDisplaySettings(NULL)) -> "desktop WxH"
// R 800x600x32 with CDS_UPDATEREGISTRY, as DESK does -> "stored WxH"
// C SetCapture(A) -> "captured"
// U ReleaseCapture -> "released"
// M TrackPopupMenu at 120,300 (TPM_RETURNCMD) -> "menu open", then
// "menu closed R,D": the result and how often A was deactivated
// L focus to the combo box in B and its list dropped -> "list open", then
// "list closed" with CBN_CLOSEUP
// S "status A,D": WM_ACTIVATE of A with and without activation, counted
// N B minimized -> "B minimized", and
// "B restored" when it comes back
// 6 800x600 in 16 bit with CDS_UPDATEREGISTRY -> "stored16 WxH"
// 9 800x600 in 8 bit with CDS_UPDATEREGISTRY -> "stored8 WxH"
// G a window C like a dialog: caption, COLOR_BTNFACE as its class
// brush, a push button; at 500,100, 300 by 200, the next one 250
// further down -> "C shown"
// X WinExec of C:\WINDOWS\CALC.EXE -> "calc started"
// W SystemParametersInfo(SPI_SETWORKAREA) with SPIF_SENDCHANGE: the
// WM_WININICHANGE with its section name goes to the windows of the
// other process as well -> "broadcast done"
// K WM_WININICHANGE with an unreadable string to CALC, which fails on
// it like a program that crashes -> "calc failed"
// Q SetCursor with the next of USER's cursors (IDC_ARROW, IDC_IBEAM, IDC_WAIT,
// IDC_CROSS, IDC_UPARROW, IDC_SIZENWSE, IDC_SIZENESW, IDC_SIZEWE,
// IDC_SIZENS, IDC_SIZEALL, IDC_NO, IDC_HAND, IDC_APPSTARTING, IDC_HELP),
// then with a cursor of its own (CreateCursor, 32 by 32, hot spot 3,5),
// then with none -> "cursor N"
// T B hidden and shown again, then the thread sleeps for three seconds
// before it reads its queue -> "B shown", "B painted"
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
API void OutputDebugStringA(const char *);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U FillRect(U, const int *, U);
API U CreateSolidBrush(U);
API U SetWindowTextA(U, const char *);
API int ChangeDisplaySettingsA(void *, U);
API int GetSystemMetrics(int);
API short GetKeyState(int);
API U SetWindowRgn(U, U, U);
API U CreateEllipticRgn(int, int, int, int);
API U SetCapture(U);
API U ReleaseCapture(void);
API U SetFocus(U);
API U SetForegroundWindow(U);
API U CreatePopupMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U TrackPopupMenu(U, U, int, int, int, U, const int *);
API U SendMessageA(U, U, U, U);
API U ShowWindow(U, int);
API void Sleep(U);
API U WinExec(const char *, U);
API U SystemParametersInfoA(U, U, void *, U);
API U FindWindowA(const char *, const char *);
API U SetCursor(U);
API U LoadCursorA(U, U);
API U CreateCursor(U, int, int, int, int, const void *, const void *);

static U red, blue, window_a, window_b, combo, popup, late;
static int activated, deactivated, minimized, wheels, dialogs, cursors;
static unsigned char cursor_and[128], cursor_xor[128];
static unsigned char mode[0x94];
static void check(U value, U code) { if (!value) ExitProcess(code); }

static char *number(char *p, int value)
{
    char digits[12];
    int n = 0;
    if (value < 0) { *p++ = '-'; value = -value; }
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (n) *p++ = digits[--n];
    return p;
}
static void title(const char *prefix, int x, int y, int pair)
{
    char text[64], *p = text;
    while (*prefix) *p++ = *prefix++;
    p = number(p, x);
    *p++ = (char)(pair ? ',' : 'x');
    p = number(p, y);
    *p = 0;
    // Keep both mouse transitions even when down and up run in one host turn.
    OutputDebugStringA(text);
    SetWindowTextA(window_a, text);
}
static void screen(const char *prefix) { title(prefix, GetSystemMetrics(0), GetSystemMetrics(1), 0); }
static int change(int width, int height, U flags)
{
    *(U *)(mode + 0x6c) = (U)width;
    *(U *)(mode + 0x70) = (U)height;
    return ChangeDisplaySettingsA(mode, flags);
}

static U __stdcall proc(U h, U m, U wp, U lp);
// The keys of the test work in C as well.
static U __stdcall dialog(U h, U m, U wp, U lp) { return m == 0x100 ? proc(window_a, m, wp, lp) : DefWindowProcA(h, m, wp, lp); }
static U __stdcall proc(U h, U m, U wp, U lp)
{
    const int x = (short)(lp & 0xffff), y = (short)(lp >> 16);
    if (m == 6 && h == window_a) { if (wp & 0xffff) ++activated; else ++deactivated; }
    if (m == 0x211 && h == window_a) SetWindowTextA(window_a, "menu open"); // WM_ENTERMENULOOP
    if (m == 0x111 && h == window_b && (wp >> 16) == 8) SetWindowTextA(window_a, "list closed"); // CBN_CLOSEUP
    if (m == 5 && h == window_b && minimized && wp == 0) { minimized = 0; SetWindowTextA(window_a, "B restored"); }
    if (m == 0x0f && h == window_b && late) { late = 0; SetWindowTextA(window_a, "B painted"); }
    if (m == 0x0f)
    {
        PAINT paint;
        const U dc = BeginPaint(h, &paint);
        FillRect(dc, &paint.left, h == window_a ? red : blue);
        EndPaint(h, &paint);
        return 0;
    }
    if (m == 0x7e && h == window_a) { screen("screen "); return 0; }
    if (m == 0x201 && h == window_a) { SetCapture(h); title("A down ", x, y, 1); return 0; }
    if (m == 0x200 && h == window_a && (wp & 1)) { title("A drag ", x, y, 1); return 0; }
    if (m == 0x202 && h == window_a) { ReleaseCapture(); title("A up ", x, y, 1); return 0; }
    if (m == 0x20a) { title("A wheel ", (short)(wp >> 16), ++wheels, 1); return 0; }
    if (m == 0x100 || m == 0x104)
    {
        if (wp == 0x2e) title("CAD ", GetKeyState(0x11) < 0, GetKeyState(0x12) < 0, 1);
        if (wp == 'P') { check(change(640, 480, 4) == 0, 7921); screen("program "); }
        if (wp == 'D') { check(ChangeDisplaySettingsA(0, 0) == 0, 7922); screen("desktop "); }
        if (wp == 'R') { check(change(800, 600, 1) == 0, 7923); screen("stored "); }
        if (wp == 'C') { SetCapture(window_a); SetWindowTextA(window_a, "captured"); }
        if (wp == 'U') { ReleaseCapture(); SetWindowTextA(window_a, "released"); }
        if (wp == 'M')
        {
            // TPM_RETURNCMD.
            const U result = TrackPopupMenu(popup, 0x100, 120, 300, 0, window_a, 0);
            title("menu closed ", (int)result, deactivated, 1);
        }
        if (wp == 'L')
        {
            SetFocus(combo);
            SendMessageA(combo, 0x14f, 1, 0); // CB_SHOWDROPDOWN
            SetWindowTextA(window_a, "list open");
        }
        if (wp == 'S') title("status ", activated, deactivated, 1);
        if (wp == '6') { *(U *)(mode + 0x68) = 16; check(change(800, 600, 1) == 0, 7931); screen("stored16 "); }
        if (wp == '9') { *(U *)(mode + 0x68) = 8; check(change(800, 600, 1) == 0, 7932); screen("stored8 "); }
        if (wp == 'G')
        {
            CLASS d = {0, (U)dialog, 0, 0, GetModuleHandleA(0), 0, 0, 16, 0, "HOST_SEAMLESS_DLG"};
            RegisterClassA(&d);
            // WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU, owned by A.
            const U c = CreateWindowExA(0, "HOST_SEAMLESS_DLG", "C", 0x90c80000, 500, 100 + 250 * dialogs++, 300, 200, window_a, 0, d.instance, 0);
            CreateWindowExA(0, "BUTTON", "OK", 0x50000000, 110, 120, 80, 24, c, 1, d.instance, 0);
            SetWindowTextA(window_a, "C shown");
        }
        if (wp == 'X') { WinExec("C:\\WINDOWS\\CALC.EXE", 1); SetWindowTextA(window_a, "calc started"); }
        if (wp == 'Q')
        {
            static const U ids[14] = {32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644, 32645, 32646, 32648, 32649,
                                      32650, 32651};
            if (cursors < 14) SetCursor(LoadCursorA(0, ids[cursors]));
            else if (cursors == 14)
            {
                // A black square of 8 by 8 at the top left, the rest transparent.
                for (int i = 0; i < 128; ++i) cursor_and[i] = 0xff;
                for (int row = 0; row < 8; ++row) cursor_and[row * 4] = 0;
                SetCursor(CreateCursor(GetModuleHandleA(0), 3, 5, 32, 32, cursor_and, cursor_xor));
            }
            else SetCursor(0);
            title("cursor ", ++cursors, 0, 1);
        }
        if (wp == 'K') { SendMessageA(FindWindowA("SciCalc", 0), 0x1a, 0, 1); SetWindowTextA(window_a, "calc failed"); }
        if (wp == 'W')
        {
            int area[4] = {0, 0, GetSystemMetrics(0), GetSystemMetrics(1)};
            SystemParametersInfoA(47, 0, area, 2);
            SetWindowTextA(window_a, "broadcast done");
        }
        if (wp == 'N') { minimized = 1; ShowWindow(window_b, 6); SetWindowTextA(window_a, "B minimized"); }
        if (wp == 'T')
        {
            ShowWindow(window_b, 0);
            late = 1;
            ShowWindow(window_b, 4); // SW_SHOWNOACTIVATE
            SetWindowTextA(window_a, "B shown");
            Sleep(3000);
        }
        return 0;
    }
    if (m == 0x10) { ExitProcess(0); return 0; }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    // Keep exact caption pixels independent of the installed gradient preference.
    check(SystemParametersInfoA(0x1009, 0, 0, 0), 7933); // SPI_SETGRADIENTCAPTIONS
    mode[0x24] = 0x94;
    *(U *)(mode + 0x28) = 0x1c0000;
    *(U *)(mode + 0x68) = 32;
    red = CreateSolidBrush(0x000000ff);
    blue = CreateSolidBrush(0x00ff0000);
    const U instance = GetModuleHandleA(0);
    CLASS c = {0, (U)proc, 0, 0, instance, 0, 0, 0, 0, "HOST_SEAMLESS"};
    check(RegisterClassA(&c), 7924);
    // WS_POPUP | WS_VISIBLE.
    window_a = CreateWindowExA(0, "HOST_SEAMLESS", "start", 0x90000000, 100, 80, 300, 200, 0, 0, instance, 0);
    check(window_a, 7925);
    check(SetWindowRgn(window_a, CreateEllipticRgn(0, 0, 300, 200), 1), 7926);
    const U b = CreateWindowExA(0, "HOST_SEAMLESS", "B", 0x90000000, 250, 180, 200, 150, 0, 0, instance, 0);
    check(b, 7927);
    window_b = b;
    // WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST.
    combo = CreateWindowExA(0, "COMBOBOX", "", 0x50200003, 20, 100, 150, 120, b, 100, instance, 0);
    check(combo, 7928);
    SendMessageA(combo, 0x143, 0, (U)"eins"); // CB_ADDSTRING
    SendMessageA(combo, 0x143, 0, (U)"zwei");
    SendMessageA(combo, 0x143, 0, (U)"drei");
    popup = CreatePopupMenu();
    check(popup, 7929);
    AppendMenuA(popup, 0, 41, "Eins");
    AppendMenuA(popup, 0, 42, "Zwei");
    SetForegroundWindow(window_a);
    SetFocus(window_a);
    screen("screen ");
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(0);
}
