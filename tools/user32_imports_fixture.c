/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The USER32 exports through the public x86 ABI. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { int left, top, right, bottom; } Rect;
typedef struct { U size, flags, hwnd, hover; } Track;
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API U GetCurrentThreadId(void);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, U);
API U GetDesktopWindow(void);
API U GetParent(U);
API U SetWindowTextA(U, const char *);
API U ReleaseDC(U, U);
API U GetDC(U);
API U GetWindowDC(U);
API U GetDCEx(U, U, U);
API U InvalidateRect(U, const Rect *, U);
API U ValidateRect(U, const Rect *);
API U GetUpdateRect(U, Rect *, U);
API U GetUpdateRgn(U, U, U);
API U InvalidateRgn(U, U, U);
API U ValidateRgn(U, U);
API U ExcludeUpdateRgn(U, U);
API U CharToOemA(const char *, char *);
API U OemToCharA(const char *, char *);
API U CharToOemBuffA(const char *, char *, U);
API U OemToCharBuffA(const char *, char *, U);
API U GetWindowWord(U, int);
API U SetWindowWord(U, int, U);
API U LoadIconA(U, const char *);
API U CopyIcon(U);
API U DestroyIcon(U);
API U AttachThreadInput(U, U, U);
API U AnimateWindow(U, U, U);
typedef struct { U cbSize; Rect rcWindow, rcClient; U dwStyle, dwExStyle, dwWindowStatus;
                 U cxWindowBorders, cyWindowBorders; unsigned short atom, version; } WindowInfo;
typedef struct { U cbSize; Rect rcTitleBar; U rgstate[6]; } TitleBarInfo;
typedef struct { U cbSize; Rect rcScrollBar; int dxyLineButton, xyThumbTop, xyThumbBottom, reserved;
                 U rgstate[6]; } ScrollBarInfo;
typedef struct { U cbSize; Rect rcItem, rcButton; U stateButton, hwndCombo, hwndItem, hwndList; } ComboBoxInfo;
typedef struct { U cbSize, flags, hCursor; int x, y; } CursorInfo;
typedef struct { U cbSize, flags, hwndActive, hwndFocus, hwndCapture, hwndMenuOwner,
                 hwndMoveSize, hwndCaret; Rect rcCaret; } GuiThreadInfo;
typedef struct { U cbSize; int cItems, cColumns, cRows, iColFocus, iRowFocus, cxItem, cyItem, x, y; } AltTabInfo;
API U GetWindowInfo(U, WindowInfo *);
API U GetTitleBarInfo(U, TitleBarInfo *);
API U GetScrollBarInfo(U, U, ScrollBarInfo *);
API U GetComboBoxInfo(U, ComboBoxInfo *);
API U GetListBoxInfo(U);
API U GetCursorInfo(CursorInfo *);
API U GetAltTabInfo(U, int, AltTabInfo *, char *, U);
API U GetGUIThreadInfo(U, GuiThreadInfo *);
API U RealGetWindowClass(U, char *, U);
API U GetSystemMetrics(int);
API U SetScrollRange(U, int, int, int, U);
API U GetClassNameA(U, char *, U);
API U SetSysColors(int, const int *, const U *);
API U SetSysColorsTemp(const U *, const U *, U);
API U GetSysColor(int);
API U GetSysColorBrush(int);
API U HiliteMenuItem(U, U, U, U);
API U CreateMDIWindowA(const char *, const char *, U, int, int, int, int, U, U, U);
API U DlgDirSelectExA(U, char *, int, int);
API U DlgDirSelectComboBoxExA(U, char *, int, int);
API U EnumPropsA(U, U);
API U EnumPropsExA(U, U, U);
API U SetPropA(U, const char *, U);
API U RemovePropA(U, const char *);
API U SetWindowRgn(U, U, U);
API U GetThreadDesktop(U);
API U SetThreadDesktop(U);
API U SetWindowsHookA(int, U);
API U UnhookWindowsHook(int, U);
API U CallNextHookEx(U, int, U, U);
API U GetAncestor(U, U);
API U GetWindowModuleFileNameA(U, char *, U);
API U GetKeyboardType(int);
API U BroadcastSystemMessageA(U, U *, U, U, U);
API U TrackMouseEvent(Track *);
API U GetDlgItemInt(U, int, U *, U);
API U PackDDElParam(U, U, U);
API U UnpackDDElParam(U, U, U *, U *);
API U FreeDDElParam(U, U);
API U ReuseDDElParam(U, U, U, U, U);
API U CreateRectRgn(int, int, int, int);
API U GetRgnBox(U, Rect *);
API U DeleteObject(U);
API U GetClipBox(U, Rect *);
typedef struct { U hwnd, message, wp, lp, time; int x, y; } Message;
API U PeekMessageA(Message *, U, U, U, U);
API int ToAscii(U, U, const unsigned char *, unsigned short *, U);
API int ToAsciiEx(U, U, const unsigned char *, unsigned short *, U, U);
API U VkKeyScanA(char);
API U OemKeyScan(U);
API U MapVirtualKeyA(U, U);
API U MapVirtualKeyExA(U, U, U);
API U GetKeyboardLayout(U);
typedef struct { unsigned short virt, key, cmd; } Accel;
API U CreateAcceleratorTableA(const Accel *, int);
API int CopyAcceleratorTableA(U, Accel *, int);
API U DestroyAcceleratorTable(U);
API int TranslateAcceleratorA(U, U, Message *);
API U ExitWindowsEx(U, U);
typedef struct { U icon; U x, y, mask, color; } IconInfo;
API U CreateIcon(U, int, int, U, U, const void *, const void *);
API U CreateCursor(U, int, int, int, int, const void *, const void *);
API U GetIconInfo(U, IconInfo *);
API U DestroyCursor(U);
typedef struct { int height, ascent, descent, internal_leading, external_leading,
                 ave_width, max_width, weight, overhang, aspect_x, aspect_y;
                 unsigned char rest[12]; } TextMetric;
API U DrawStateA(U, U, U, U, U, int, int, int, int, U);
API U GrayStringA(U, U, U, U, int, int, int, int, int);
API U TabbedTextOutA(U, int, int, const char *, int, int, const int *, int);
API U GetTabbedTextExtentA(U, const char *, int, int, const int *);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U, int, int);
API U SelectObject(U, U);
API U DeleteDC(U);
API U GetPixel(U, int, int);
API U PatBlt(U, int, int, int, int, U);
API U TextOutA(U, int, int, const char *, int);
API U GetTextMetricsA(U, TextMetric *);
API U SetTextColor(U, U);
API U SetBkColor(U, U);
API U CreateSolidBrush(U);
API U DlgDirListA(U, char *, int, int, U);
API void RegisterSystemThread(U, U);
API void RegisterNetworkCapabilities(U, U);
API U SendMessageA(U, U, U, U);
API U EnumDisplaySettingsA(const char *, U, void *);
API U ChangeDisplaySettingsExA(const char *, void *, U, U, U);
API U ArrangeIconicWindows(U);
API U TileWindows(U, U, const Rect *, U, const U *);
API U CascadeWindows(U, U, const Rect *, U, const U *);
API U ShowOwnedPopups(U, U);
API U FlashWindow(U, U);
API U ShowWindowAsync(U, U);
API U IsWindowVisible(U);
typedef struct { U size, hwnd, flags, count, timeout; } FlashInfo;
API U FlashWindowEx(const FlashInfo *);
API U ChangeMenuA(U, U, const char *, U, U);
API U LoadMenuIndirectA(const void *);
API U CreateMenu(void);
API U CreatePopupMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U DestroyMenu(U);
API int GetMenuItemCount(U);
API int GetMenuStringA(U, U, char *, int, U);
API U GetMenuItemID(U, int);
API U GetMenuState(U, U, U);
typedef struct { U size, type, reserved; unsigned char guid[16]; char name[4]; } DeviceFilter;
API U RegisterDeviceNotificationA(U, const void *, U);
API U UnregisterDeviceNotification(U);
API U EndTask(U, U, const char *, U);
API U DefFrameProcA(U, U, U, U, U);
API U DefMDIChildProcA(U, U, U, U);
API U TranslateMDISysAccel(U, Message *);
API U GetWindowRect(U, int *);
API U GetWindowTextA(U, char *, int);
API U SetMenu(U, U);
API U GetActiveWindow(void);
API U SetFocus(U);
typedef struct { U menu, first; } ClientCreate;
typedef struct { U klass, title, instance; int x, y, cx, cy; U style, param; } MdiCreate;
static U mdi_client;
static U __stdcall frame_proc(U h, U m, U w, U l)
{
    return DefFrameProcA(h, mdi_client, m, w, l);
}
static U __stdcall child_proc(U h, U m, U w, U l) { return DefMDIChildProcA(h, m, w, l); }

static U parent, child, hook, hook_calls;
static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static int same(const char *a, const char *b) { while (*a && *a == *b) ++a, ++b; return *a == *b; }
static U __stdcall proc(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static U prop_seen, prop_last_data, prop_parameter, prop_stop;
static U __stdcall prop_enum(U h, U name, U data)
{
    (void)h; (void)name;
    ++prop_seen; prop_last_data = data;
    return prop_stop ? 0 : 1;
}
static U __stdcall prop_enum_ex(U h, U name, U data, U parameter)
{
    (void)h; (void)name;
    ++prop_seen; prop_last_data = data; prop_parameter = parameter;
    return 1;
}
static U __stdcall filter(int code, U w, U l) { ++hook_calls; return CallNextHookEx(hook, code, w, l); }
/* The end of session: the window counts both messages and can deny the first. */
static U session_veto, session_queries, session_ends, session_lparam, session_wparam;
static U __stdcall session_proc(U h, U m, U w, U l)
{
    if (m == 0x11) { ++session_queries; session_lparam = l; return session_veto ? 0 : 1; }
    if (m == 0x16) { ++session_ends; session_wparam = w; return 0; }
    return DefWindowProcA(h, m, w, l);
}

void run_user32_imports_test(void)
{
    /* Class with four extra window bytes for GetWindowWord and SetWindowWord. */
    U cls[10] = {0, (U)proc, 0, 4, 0, 0, 0, 0, 0, (U)"User32Imports98"};
    check(RegisterClassA(cls) != 0, 14000);
    parent = CreateWindowExA(0, "User32Imports98", "parent", 0x00cf0000, 40, 50, 260, 180, 0, 0, 0, 0);
    check(parent != 0, 14001);
    ShowWindow(parent, 5);
    child = CreateWindowExA(0, "User32Imports98", "child", 0x40000000, 10, 12, 80, 60, parent, 0, 0, 0);
    check(child != 0, 14002);
    ShowWindow(child, 5);

    /* GetWindowDC leases the frame, GetDC the client, GetDCEx both. */
    U window_dc = GetWindowDC(parent);
    check(window_dc != 0, 14003);
    Rect frame_box = {0, 0, 0, 0};
    check(GetClipBox(window_dc, &frame_box) != 0, 14004);
    check(ReleaseDC(parent, window_dc) != 0, 14005);
    U client_dc = GetDC(parent);
    check(client_dc != 0, 14006);
    Rect client_box = {0, 0, 0, 0};
    check(GetClipBox(client_dc, &client_box) != 0, 14007);
    check(ReleaseDC(parent, client_dc) != 0, 14008);
    /* The frame is at least as large as the client area. */
    check(frame_box.right - frame_box.left >= client_box.right - client_box.left, 14009);
    check(frame_box.bottom - frame_box.top >= client_box.bottom - client_box.top, 14010);
    U style_dc = GetDCEx(parent, 0, 0x00010000);
    check(style_dc != 0, 14011);
    check(ReleaseDC(parent, style_dc) != 0, 14012);
    U frame_dc = GetDCEx(parent, 0, 0x00010001);
    check(frame_dc != 0, 14013);
    check(ReleaseDC(parent, frame_dc) != 0, 14014);
    /* A rejected DCX bit and a handle that is not a region both fail. */
    SetLastError(0);
    check(GetDCEx(parent, 0, 0x00000004) == 0 && GetLastError() == 87, 14015);
    SetLastError(0);
    check(GetDCEx(parent, parent, 0x00000080) == 0 && GetLastError() == 87, 14016);

    /* The update region as a GDI region and as a clip exclusion. */
    U region = CreateRectRgn(0, 0, 1, 1), piece = CreateRectRgn(4, 6, 24, 26);
    check(region != 0 && piece != 0, 14017);
    check(ValidateRect(parent, 0) != 0, 14018);
    check(GetUpdateRgn(parent, region, 0) == 1, 14019);
    Rect box = {1, 1, 1, 1};
    check(GetRgnBox(region, &box) == 1 && box.left == box.right, 14020);
    check(InvalidateRgn(parent, piece, 0) != 0, 14021);
    check(GetUpdateRgn(parent, region, 0) == 2, 14022);
    check(GetRgnBox(region, &box) == 2, 14023);
    check(box.left == 4 && box.top == 6 && box.right == 24 && box.bottom == 26, 14024);
    U paint_dc = GetDC(parent);
    check(paint_dc != 0, 14025);
    Rect before = {0, 0, 0, 0}, after = {0, 0, 0, 0};
    check(GetClipBox(paint_dc, &before) != 0, 14026);
    check(ExcludeUpdateRgn(paint_dc, parent) != 0, 14027);
    check(GetClipBox(paint_dc, &after) != 0, 14028);
    check(after.top >= before.top, 14029);
    check(ReleaseDC(parent, paint_dc) != 0, 14030);
    check(ValidateRgn(parent, piece) != 0, 14031);
    check(GetUpdateRect(parent, &box, 0) == 0, 14032);
    /* ExcludeUpdateRgn without an update region answers the clip box. */
    paint_dc = GetDC(parent);
    check(paint_dc != 0 && ExcludeUpdateRgn(paint_dc, parent) != 0, 14033);
    check(ReleaseDC(parent, paint_dc) != 0, 14034);
    SetLastError(0);
    check(GetUpdateRgn(parent, parent, 0) == 0 && GetLastError() == 87, 14035);
    DeleteObject(region);
    DeleteObject(piece);

    /* The four OEM translations, including the three DOS control glyphs. */
    char oem[16] = {0}, ansi[16] = {0};
    check(CharToOemA("Abc", oem) != 0 && same(oem, "Abc"), 14036);
    check(OemToCharA("Abc", ansi) != 0 && same(ansi, "Abc"), 14037);
    const char control[4] = {0x0f, 0x14, 0x15, 0};
    check(OemToCharA(control, ansi) != 0, 14038);
    check((unsigned char)ansi[0] == 0xa4 && (unsigned char)ansi[1] == 0xb6 &&
          (unsigned char)ansi[2] == 0xa7 && !ansi[3], 14039);
    char buffer[4] = {'x', 'y', 'z', 'w'};
    check(CharToOemBuffA("abcd", buffer, 4) != 0 && buffer[3] == 'd', 14040);
    check(OemToCharBuffA("abcd", buffer, 2) != 0 && buffer[0] == 'a' && buffer[2] == 'c', 14041);

    /* Negative reads use the low word of GetWindowLong; invalid offsets preserve LastError. */
    check(SetWindowWord(parent, 0, 0x1234) == 0, 14042);
    check(GetWindowWord(parent, 0) == 0x1234, 14043);
    check(SetWindowWord(parent, 2, 0xabcd) == 0, 14044);
    check(GetWindowWord(parent, 2) == 0xabcd, 14045);
    check(GetWindowWord(parent, 0) == 0x1234, 14046);
    SetLastError(0xdeadbeef);
    check(GetWindowWord(parent, -4) == ((U)proc & 0xffff) && GetLastError() == 0xdeadbeef, 14047);
    SetLastError(0xdeadbeef);
    check(SetWindowWord(parent, 4, 1) == 0 && GetLastError() == 0xdeadbeef, 14048);

    /* CopyIcon hands out an independent icon. */
    U icon = LoadIconA(0, (const char *)32512);
    if (icon)
    {
        U copy = CopyIcon(icon);
        check(copy != 0 && copy != icon, 14049);
        check(DestroyIcon(copy) != 0, 14050);
    }

    /* One native input queue: the same thread pair is refused, an unknown
       thread is refused, and a live pair is already sharing. */
    SetLastError(0);
    check(AttachThreadInput(GetCurrentThreadId(), GetCurrentThreadId(), 1) == 0 &&
          GetLastError() == 87, 14099);
    SetLastError(0);
    check(AttachThreadInput(GetCurrentThreadId(), 0x7fffffff, 1) == 0 &&
          GetLastError() == 1444, 14100);

    /* Desktops: one token for every thread, and the set always succeeds. */
    check(GetThreadDesktop(GetCurrentThreadId()) == 0x12345678, 14051);
    check(GetThreadDesktop(0) == 0x12345678, 14052);
    check(SetThreadDesktop(0x12345678) == 1, 14053);

    /* SetWindowsHookA installs on the calling thread; the pair removes it. */
    hook = SetWindowsHookA(-1, (U)filter);
    check(hook != 0, 14054);
    check(UnhookWindowsHook(-1, (U)filter) != 0, 14055);
    SetLastError(0);
    check(UnhookWindowsHook(-1, (U)filter) == 0 && GetLastError() == 1404, 14056);

    /* GetAncestor: the parent alone, then the root of the chain. */
    check(GetAncestor(child, 1) == parent, 14057);
    check(GetAncestor(child, 2) == parent, 14058);
    check(GetAncestor(parent, 2) == parent, 14059);
    check(GetAncestor(parent, 1) == GetParent(parent), 14060);
    SetLastError(0);
    check(GetAncestor(0x7fff, 2) == 0 && GetLastError() == 1400, 14061);

    /* The module file name of a window of this process. */
    char path[64] = {0};
    check(GetWindowModuleFileNameA(parent, path, sizeof path) != 0 && path[0], 14062);

    /* The enhanced keyboard: type 4, subtype 0, twelve function keys. */
    check(GetKeyboardType(0) == 4, 14063);
    check(GetKeyboardType(1) == 0, 14064);
    check(GetKeyboardType(2) == 12, 14065);
    SetLastError(0);
    check(GetKeyboardType(3) == 0 && GetLastError() == 87, 14066);

    /* A broadcast to the application windows, and a refused recipient class. */
    U recipients = 8;
    check(BroadcastSystemMessageA(0x10, &recipients, 0x0400, 0, 0) != 0, 14067);
    /* The posted form queues the message at every top-level window of the
       process; take the copies back out so the next fixture sees a clean
       queue. */
    Message posted;
    U delivered = 0;
    while (PeekMessageA(&posted, 0, 0x0400, 0x0400, 1)) ++delivered;
    check(delivered != 0, 14067);
    recipients = 4; /* BSM_INSTALLABLEDRIVERS */
    SetLastError(0);
    check(BroadcastSystemMessageA(0, &recipients, 0x0400, 0, 0) == 0xffffffffu &&
          GetLastError() == 120, 14068);

    /* Mouse leave tracking, its query form and the missing hover producer. */
    Track track = {sizeof(Track), 2, parent, 0};
    check(TrackMouseEvent(&track) != 0, 14069);
    Track query = {sizeof(Track), 0x40000000, parent, 0};
    check(TrackMouseEvent(&query) != 0 && query.flags == 2, 14070);
    Track cancel = {sizeof(Track), 0x80000002u, parent, 0};
    check(TrackMouseEvent(&cancel) != 0, 14071);
    query.flags = 0x40000000;
    check(TrackMouseEvent(&query) != 0 && query.flags == 0, 14072);
    Track hover = {sizeof(Track), 1, parent, 100};
    SetLastError(0);
    check(TrackMouseEvent(&hover) == 0 && GetLastError() == 120, 14073);
    Track wrong = {8, 2, parent, 0};
    SetLastError(0);
    check(TrackMouseEvent(&wrong) == 0 && GetLastError() == 87, 14074);

    /* GetDlgItemInt parses the control text, signed and unsigned. */
    check(SetWindowTextA(child, "  4711") != 0, 14075);
    U translated = 0xff;
    check(GetDlgItemInt(parent, 0, &translated, 0) == 4711 && translated == 1, 14076);
    check(SetWindowTextA(child, "-27") != 0, 14077);
    check(GetDlgItemInt(parent, 0, &translated, 1) == (U)-27 && translated == 1, 14078);
    check(GetDlgItemInt(parent, 0, &translated, 0) == 0 && translated == 0, 14079);
    check(SetWindowTextA(child, "12x") != 0, 14080);
    check(GetDlgItemInt(parent, 0, &translated, 0) == 0 && translated == 0, 14081);
    check(SetWindowTextA(child, "4294967296") != 0, 14082);
    check(GetDlgItemInt(parent, 0, &translated, 0) == 0 && translated == 0, 14083);

    /* The DDE lParam pool. */
    U low = 0, high = 0;
    U plain = PackDDElParam(0x3e0, 0x1111, 0x2222);
    check(plain == 0x22221111, 14084);
    check(UnpackDDElParam(0x3e0, plain, &low, &high) != 0 && low == 0x1111 && high == 0x2222, 14085);
    check(FreeDDElParam(0x3e0, plain) != 0, 14086);
    U execute = PackDDElParam(0x3e8, 0, 0xdeadbeef);
    check(execute == 0xdeadbeef, 14087);
    check(UnpackDDElParam(0x3e8, execute, &low, &high) != 0 && low == 0 && high == 0xdeadbeef, 14088);
    U acknowledge = PackDDElParam(0x3e4, 0x8001, 0x9002);
    check(acknowledge != 0 && acknowledge != 0x90028001, 14089);
    low = high = 0;
    check(UnpackDDElParam(0x3e4, acknowledge, &low, &high) != 0, 14090);
    check(low == 0x8001 && high == 0x9002, 14091);
    U again = ReuseDDElParam(acknowledge, 0x3e4, 0x3e5, 0x1234, 0x5678);
    check(again == acknowledge, 14092);
    check(UnpackDDElParam(0x3e5, again, &low, &high) != 0 && low == 0x1234 && high == 0x5678, 14093);
    check(FreeDDElParam(0x3e5, again) != 0, 14094);
    check(FreeDDElParam(0x3e5, again) == 0, 14095);
    check(UnpackDDElParam(0x3e5, again, &low, &high) == 0 && !low && !high, 14096);
    /* A message the pool does not know is refused instead of packed blindly. */
    SetLastError(0);
    check(PackDDElParam(0x0400, 1, 2) == 0 && GetLastError() == 87, 14097);

    /* The keyboard translation of KEYBOARD.DRV over the installed layout. */
    U layout = GetKeyboardLayout(0);
    check((layout & 0xffff) == 0x0409 || (layout & 0xffff) == 0x0407, 14100);
    if ((layout & 0xffff) == 0x0409) {
        /* VkKeyScan answers the shift state over the virtual key. */
        check(VkKeyScanA('a') == 0x0041, 14101);
        check(VkKeyScanA('A') == 0x0141, 14102);
        check(VkKeyScanA('~') == 0x01c0, 14103);
        check(VkKeyScanA(';') == 0x00ba, 14104);
        /* Control codes come from the driver's own tables, not the layout. */
        check(VkKeyScanA(1) == 0x0241, 14105);
        /* Escape is the layout's own control state, CONTROL over the bracket. */
        check(VkKeyScanA(27) == 0x02db, 14106);
        check(VkKeyScanA('5') == 0x0035, 14107);
        /* A character the layout cannot produce answers FFFFh. */
        check(VkKeyScanA((char)0xfc) == 0xffff, 14108);

        /* OemKeyScan packs the shift state over the scan code. */
        check(OemKeyScan('a') == 0x0000001e, 14109);
        check(OemKeyScan('A') == 0x0002001e, 14110);
        check(OemKeyScan(1) == 0x0004001e, 14111);
        check(OemKeyScan(0xfc) == 0xffffffff, 14112);

        {
            unsigned char state[256];
            unsigned short out;
            U i;
            for (i = 0; i < 256; ++i) state[i] = 0;
            /* A plain key, then the same key with shift, control and caps. */
            out = 0;
            check(ToAscii('A', 0x1e, state, &out, 0) == 1 && out == 'a', 14113);
            state[0x10] = 0x80;
            out = 0;
            check(ToAscii('A', 0x1e, state, &out, 0) == 1 && out == 'A', 14114);
            state[0x10] = 0;
            state[0x11] = 0x80;
            out = 0;
            check(ToAscii('A', 0x1e, state, &out, 0) == 1 && out == 1, 14115);
            /* Control over a bracket comes from the layout's own state table. */
            out = 0;
            check(ToAscii(0xdb, 0x1a, state, &out, 0) == 1 && out == 0x1b, 14116);
            state[0x11] = 0;
            state[0x14] = 1;
            out = 0;
            check(ToAscii('A', 0x1e, state, &out, 0) == 1 && out == 'A', 14117);
            /* Caps lock does not shift a digit in this layout. */
            out = 0;
            check(ToAscii('1', 0x02, state, &out, 0) == 1 && out == '1', 14118);
            state[0x14] = 0;
            /* Escape and the numeric pad answer from the driver's tables. */
            out = 0;
            check(ToAscii(0x1b, 0x01, state, &out, 0) == 1 && out == 27, 14119);
            out = 0;
            check(ToAscii(0x6b, 0x4e, state, &out, 0) == 1 && out == '+', 14120);
            /* A key that produces nothing, and a key going up. */
            out = 0xffff;
            check(ToAscii(0x70, 0x3b, state, &out, 0) == 0, 14121);
            out = 0;
            check(ToAscii('A', 0x1e | 0x8000, state, &out, 0) == 0, 14122);
            /* ToAsciiEx takes the layout; one that is not installed fails. */
            out = 0;
            check(ToAsciiEx('B', 0x30, state, &out, 0, layout) == 1 && out == 'b', 14123);
            out = 0xffff;
            check(ToAsciiEx('B', 0x30, state, &out, 0, 0x04010401) == 0, 14124);
        }

        /* MapVirtualKeyEx is MapVirtualKey behind a layout lookup. */
        check(MapVirtualKeyExA('A', 0, layout) == MapVirtualKeyA('A', 0), 14125);
        check(MapVirtualKeyExA('A', 0, 0) == MapVirtualKeyA('A', 0), 14126);
        check(MapVirtualKeyExA('A', 0, 0x04010401) == 0, 14127);
    }

    /* Accelerator tables: the store, the terminator and the copy rules. */
    {
        Accel table[3];
        Accel copy[4];
        Message key;
        U accel;
        int i;
        table[0].virt = 1 | 8; table[0].key = 'S'; table[0].cmd = 0x1001;   /* Ctrl+S */
        table[1].virt = 1;     table[1].key = 0x70; table[1].cmd = 0x1002;  /* F1 */
        table[2].virt = 0;     table[2].key = 'q'; table[2].cmd = 0x1003;   /* the character q */
        SetLastError(0);
        check(CreateAcceleratorTableA(0, 3) == 0, 14130);
        check(CreateAcceleratorTableA(table, 0) == 0, 14131);
        accel = CreateAcceleratorTableA(table, 3);
        check(accel != 0, 14132);
        /* A null buffer and a count that is not positive both answer the size. */
        check(CopyAcceleratorTableA(accel, 0, 0) == 3, 14133);
        check(CopyAcceleratorTableA(accel, copy, 0) == 3, 14134);
        /* A short copy keeps the entries it took and stops there. */
        for (i = 0; i < 4; ++i) copy[i].virt = copy[i].key = copy[i].cmd = 0xffff;
        check(CopyAcceleratorTableA(accel, copy, 2) == 2, 14135);
        check(copy[0].virt == (1 | 8) && copy[0].key == 'S' && copy[0].cmd == 0x1001, 14136);
        check(copy[1].virt == 1 && copy[1].key == 0x70, 14137);
        check(copy[2].virt == 0xffff, 14138);
        /* A full copy takes the terminator bit back out of the last entry. */
        for (i = 0; i < 4; ++i) copy[i].virt = copy[i].key = copy[i].cmd = 0xffff;
        check(CopyAcceleratorTableA(accel, copy, 4) == 3, 14139);
        check(copy[2].virt == 0 && copy[2].key == 'q' && copy[2].cmd == 0x1003, 14140);
        check(copy[3].virt == 0xffff, 14141);
        /* TranslateAccelerator reads the created table, not a resource. */
        key.hwnd = parent; key.message = 0x100; key.wp = 0x70; key.lp = 0;
        key.time = 0; key.x = 0; key.y = 0;
        check(TranslateAcceleratorA(parent, accel, &key) == 1, 14142);
        key.message = 0x102; key.wp = 'q';
        check(TranslateAcceleratorA(parent, accel, &key) == 1, 14143);
        key.message = 0x100; key.wp = 'Z';
        check(TranslateAcceleratorA(parent, accel, &key) == 0, 14144);
        key.message = 0x200;
        check(TranslateAcceleratorA(parent, accel, &key) == 0, 14145);
        /* The first destroy frees the block, the second finds nothing. */
        check(DestroyAcceleratorTable(accel) != 0, 14146);
        check(DestroyAcceleratorTable(accel) == 0, 14147);
        check(DestroyAcceleratorTable(0) == 0, 14148);
        check(CopyAcceleratorTableA(accel, 0, 0) == 0, 14149);
    }

    /* USER walks the list the display driver builds. The driver of
       NEO98 is the screen surface of the compositor, so the list is 640x480
       and 800x600 at 8, 16, 24 and 32 bits, and the walk ends after the
       eighth entry. -1 is the mode that is up. */
    {
        unsigned char mode[148];
        U *fields = (U *)(mode + 40);
        int i;
        for (i = 0; i < 148; ++i) mode[i] = 0;
        check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0, 14230);
        check(*(unsigned short *)(mode + 36) == 148, 14231);
        check(*(U *)(mode + 104) == 32, 14232);
        check(*(U *)(mode + 108) == 800 && *(U *)(mode + 112) == 600, 14233);
        check((*fields & 0x001c0000) == 0x001c0000, 14234);
        check(EnumDisplaySettingsA(0, 0, mode) != 0, 14235);
        check(*(U *)(mode + 108) == 640 && *(U *)(mode + 112) == 480 &&
              *(U *)(mode + 104) == 8, 14242);
        check(EnumDisplaySettingsA(0, 7, mode) != 0, 14243);
        check(*(U *)(mode + 108) == 800 && *(U *)(mode + 112) == 600 &&
              *(U *)(mode + 104) == 32, 14244);
        /* The list also carries 1024x768 as well, so it has twelve
           entries: three sizes at 8, 16, 24 and 32 bits. */
        check(EnumDisplaySettingsA(0, 11, mode) != 0, 14248);
        check(*(U *)(mode + 108) == 1024 && *(U *)(mode + 112) == 768 &&
              *(U *)(mode + 104) == 32, 14249);
        check(EnumDisplaySettingsA(0, 12, mode) == 0, 14236);
        /* The mode that is up needs no change; a mode outside the list is
           BADMODE even when only asked for with CDS_TEST. */
        check(EnumDisplaySettingsA(0, 7, mode) != 0, 14250);
        check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == 0, 14237);
        check(ChangeDisplaySettingsExA(0, 0, 0, 0, 0) == 0, 14238);
        *(U *)(mode + 108) = 1280; *(U *)(mode + 112) = 1024;
        check(ChangeDisplaySettingsExA(0, mode, 0, 2, 0) == (U)-2, 14239);
        check(ChangeDisplaySettingsExA(0, mode, 0, 0, 0) == (U)-2, 14245);
        /* 640x480 at 8 bits is in the list, and CDS_TEST leaves the screen
           where it is. */
        *(U *)(mode + 108) = 640; *(U *)(mode + 112) = 480; *(U *)(mode + 104) = 8;
        check(ChangeDisplaySettingsExA(0, mode, 0, 2, 0) == 0, 14246);
        check(EnumDisplaySettingsA(0, 0xffffffff, mode) != 0 &&
              *(U *)(mode + 108) == 800 && *(U *)(mode + 104) == 32, 14247);
        /* A window is not an argument of this export, and stray flags fail. */
        check(ChangeDisplaySettingsExA(0, mode, parent, 0, 0) == (U)-5, 14240);
        check(ChangeDisplaySettingsExA(0, mode, 0, 0x01000000, 0) == (U)-4, 14241);
        /* Neither registration entry has a return value of its own. */
        RegisterSystemThread(1, 0);
        RegisterNetworkCapabilities(0xffff, 0);
    }

    /* Arranging: USER, and over the children of one
       parent. A minimized child joins the icon grid, a captioned one joins the
       tile, and a window without a caption joins neither. */
    {
        U a1, a2, owned;
        Rect box;
        a1 = CreateWindowExA(0, "User32Imports98", "tile one", 0x40cf0000,
                             4, 4, 60, 40, parent, 0, 0, 0);
        a2 = CreateWindowExA(0, "User32Imports98", "tile two", 0x40cf0000,
                             8, 8, 60, 40, parent, 0, 0, 0);
        check(a1 != 0 && a2 != 0, 14250);
        ShowWindow(a1, 5);
        ShowWindow(a2, 5);
        check(TileWindows(parent, 0, 0, 0, 0) == 2, 14251);
        check(CascadeWindows(parent, 0, 0, 0, 0) == 2, 14252);
        /* The caller's rectangle is taken as it is. */
        box.left = 0; box.top = 0; box.right = 100; box.bottom = 80;
        check(TileWindows(parent, 1, &box, 0, 0) == 2, 14253);
        /* An explicit list is honoured. */
        {
            U kids[1];
            kids[0] = a1;
            check(CascadeWindows(parent, 0, 0, 1, kids) == 1, 14254);
        }
        /* A minimized child leaves the tile and joins the icon grid. */
        ShowWindow(a1, 6);
        check(TileWindows(parent, 0, 0, 0, 0) == 1, 14255);
        check(ArrangeIconicWindows(parent) == 1, 14256);
        ShowWindow(a1, 9);
        /* ShowWindowAsync shows the window. */
        ShowWindow(a2, 0);
        check(IsWindowVisible(a2) == 0, 14257);
        ShowWindowAsync(a2, 5);
        check(IsWindowVisible(a2) != 0, 14258);
        /* An owned popup follows its owner, and the owner itself does not. */
        owned = CreateWindowExA(0, "User32Imports98", "popup", 0x80000000,
                                200, 200, 60, 40, 0, 0, 0, 0);
        check(owned != 0, 14259);
        ShowWindow(owned, 5);
        check(IsWindowVisible(owned) != 0, 14260);
        check(ShowOwnedPopups(parent, 0) != 0, 14261);
        check(IsWindowVisible(parent) != 0, 14262);
        check(ShowOwnedPopups(parent, 1) != 0, 14263);
        /* FlashWindow answers the caption state it found and toggles it. */
        check(FlashWindow(parent, 1) == 0, 14264);
        check(FlashWindow(parent, 1) == 1, 14265);
        check(FlashWindow(parent, 0) == 0, 14266);
        check(DestroyWindow(owned) != 0, 14267);
        check(DestroyWindow(a1) != 0, 14268);
        check(DestroyWindow(a2) != 0, 14269);
    }

    /* DlgDirListA over the native LB_DIR: USER changes the directory,
       writes the remaining pattern back into the caller's buffer and sends
       LB_DIR twice, once for the files and once for the directories and the
       drives. Directories come back as [NAME], drives as [-x-]. */
    {
        U list, count, i, bracketed = 0, drive = 0;
        char spec[132];
        char entry[132];
        list = CreateWindowExA(0, "LISTBOX", "", 0x50200002, 0, 0, 120, 80,
                               parent, 2001, 0, 0);
        check(list != 0, 14210);
        /* Only the drives: every drive is [-x-] and nothing else is listed. */
        check(SendMessageA(list, 0x184, 0, 0) == 0, 14211);   /* LB_RESETCONTENT */
        SendMessageA(list, 0x18d, 0xd000, (U)"*.*");          /* LB_DIR, drives only */
        count = SendMessageA(list, 0x18b, 0, 0);              /* LB_GETCOUNT */
        check(count > 0, 14212);
        for (i = 0; i < count; ++i)
        {
            entry[0] = 0;
            SendMessageA(list, 0x189, i, (U)entry);           /* LB_GETTEXT */
            check(entry[0] == '[' && entry[1] == '-', 14213);
            ++drive;
        }
        check(drive == count, 14214);
        /* The full list: the directory entries carry brackets. */
        spec[0] = '*'; spec[1] = '.'; spec[2] = '*'; spec[3] = 0;
        check(DlgDirListA(parent, spec, 2001, 0, 0x4010) != 0, 14215);
        check(same(spec, "*.*"), 14216);
        count = SendMessageA(list, 0x18b, 0, 0);
        check(count > 0, 14217);
        bracketed = 0;
        for (i = 0; i < count; ++i)
        {
            entry[0] = 0;
            SendMessageA(list, 0x189, i, (U)entry);
            if (entry[0] == '[') ++bracketed;
        }
        check(bracketed > 0, 14218);
        /* A path with a directory in front leaves only the pattern behind. */
        spec[0] = 'C'; spec[1] = ':'; spec[2] = '\\'; spec[3] = '*';
        spec[4] = '.'; spec[5] = '*'; spec[6] = 0;
        check(DlgDirListA(parent, spec, 2001, 0, 0) != 0, 14219);
        check(same(spec, "*.*"), 14220);
        /* A path longer than 80h characters is refused. */
        for (i = 0; i < 130; ++i) spec[i] = 'x';
        spec[130] = 0;
        check(DlgDirListA(parent, spec, 2001, 0, 0) == 0, 14221);
        check(DestroyWindow(list) != 0, 14222);
    }

    /* DrawState, GrayString and the tabbed text pair. The gray pattern knocks every second pixel out of the
       monochrome image, so the grayed text covers less than the plain one. */
    {
        U screen = GetDC(0), mem, bitmap, keep, black;
        TextMetric metrics;
        U plain_extent, tab_extent, drawn, grayed;
        int px, py;
        check(screen != 0, 14180);
        mem = CreateCompatibleDC(screen);
        check(mem != 0, 14181);
        bitmap = CreateCompatibleBitmap(screen, 200, 60);
        check(bitmap != 0, 14182);
        keep = SelectObject(mem, bitmap);
        check(GetTextMetricsA(mem, &metrics) != 0, 14183);
        check(metrics.ave_width > 0, 14184);
        /* Without a stop list a tab is eight average characters wide. */
        plain_extent = GetTabbedTextExtentA(mem, "AB", 2, 0, 0);
        tab_extent = GetTabbedTextExtentA(mem, "A\tB", 3, 0, 0);
        check((plain_extent & 0xffff) > 0, 14185);
        check((tab_extent & 0xffff) >= (U)(8 * metrics.ave_width), 14186);
        check((tab_extent >> 16) == (U)metrics.height, 14187);
        /* One stop is a fixed width. */
        {
            int stops[1];
            stops[0] = 100;
            tab_extent = GetTabbedTextExtentA(mem, "A\tB", 3, 1, stops);
            check((tab_extent & 0xffff) >= 100, 14188);
        }
        SetTextColor(mem, 0);
        SetBkColor(mem, 0xffffff);
        check(PatBlt(mem, 0, 0, 200, 60, 0x00ff0062) != 0, 14189);
        check(TabbedTextOutA(mem, 0, 0, "A\tB", 3, 0, 0, 0) != 0, 14190);
        drawn = 0;
        for (py = 0; py < metrics.height; ++py)
            for (px = 0; px < 200; ++px)
                if (GetPixel(mem, px, py) != 0xffffff) ++drawn;
        check(drawn > 0, 14191);
        /* GrayStringA is DrawState with DST_TEXT, the callback bit and
           DSS_UNION, so it goes through the monochrome bitmap. */
        black = CreateSolidBrush(0);
        check(black != 0, 14192);
        check(PatBlt(mem, 0, 0, 200, 60, 0x00ff0062) != 0, 14193);
        check(TextOutA(mem, 0, 0, "MMMMMM", 6) != 0, 14194);
        drawn = 0;
        for (py = 0; py < metrics.height; ++py)
            for (px = 0; px < 200; ++px)
                if (GetPixel(mem, px, py) != 0xffffff) ++drawn;
        check(drawn > 0, 14195);
        check(PatBlt(mem, 0, 0, 200, 60, 0x00ff0062) != 0, 14196);
        check(GrayStringA(mem, black, 0, (U)"MMMMMM", 6, 0, 0, 0, 0) != 0, 14197);
        grayed = 0;
        for (py = 0; py < metrics.height; ++py)
            for (px = 0; px < 200; ++px)
                if (GetPixel(mem, px, py) != 0xffffff) ++grayed;
        check(grayed > 0 && grayed < drawn, 14198);
        /* DrawStateA without a type and without a callback answers zero. */
        check(DrawStateA(mem, 0, 0, 0, 0, 0, 0, 10, 10, 0) == 0, 14199);
        /* A type above five is not a type. */
        check(DrawStateA(mem, 0, 0, (U)"x", 1, 0, 0, 10, 10, 6) == 0, 14200);
        SelectObject(mem, keep);
        DeleteObject(bitmap);
        DeleteDC(mem);
        DeleteObject(black);
        ReleaseDC(0, screen);
    }

    /* CreateIcon and CreateCursor: USER and over.
       A 16 pixel row is two bytes wide, the icon takes its hot spot from its
       middle and the cursor from the caller, and both planes run top down. */
    {
        unsigned char and_plane[32], xor_plane[32];
        IconInfo info;
        U hicon, hcursor;
        int i;
        for (i = 0; i < 32; ++i) { and_plane[i] = 0; xor_plane[i] = 0; }
        xor_plane[0] = 0xff;            /* row 0, the left eight pixels white */
        and_plane[2] = 0x80;            /* row 1, pixel 0 transparent */
        hicon = CreateIcon(0, 16, 16, 1, 1, and_plane, xor_plane);
        check(hicon != 0, 14161);
        info.icon = info.x = info.y = info.mask = info.color = 0;
        check(GetIconInfo(hicon, &info) != 0, 14162);
        check(info.icon == 1 && info.x == 8 && info.y == 8, 14163);
        check(info.mask != 0 && info.color != 0, 14164);
        DeleteObject(info.mask);
        DeleteObject(info.color);
        check(DestroyIcon(hicon) != 0, 14165);
        hcursor = CreateCursor(0, 3, 4, 16, 16, and_plane, xor_plane);
        check(hcursor != 0, 14166);
        info.icon = info.x = info.y = info.mask = info.color = 0;
        check(GetIconInfo(hcursor, &info) != 0, 14167);
        check(info.icon == 0 && info.x == 3 && info.y == 4, 14168);
        DeleteObject(info.mask);
        DeleteObject(info.color);
        check(DestroyCursor(hcursor) != 0, 14169);
        /* refuses a hot spot outside the cursor. */
        check(CreateCursor(0, 17, 4, 16, 16, and_plane, xor_plane) == 0, 14170);
        check(CreateCursor(0, 3, -1, 16, 16, and_plane, xor_plane) == 0, 14171);
        /* More than one plane has no native reading; see USER32-IMPORTS.md. */
        SetLastError(0);
        check(CreateIcon(0, 16, 16, 2, 1, and_plane, xor_plane) == 0 &&
              GetLastError() == 87, 14172);
        SetLastError(0);
        check(CreateIcon(0, 16, 16, 1, 8, and_plane, xor_plane) == 0 &&
              GetLastError() == 87, 14173);
    }

    /* ExitWindowsEx. The flags decide the lParam of
       both messages, a denied WM_QUERYENDSESSION stops the walk and still
       produces WM_ENDSESSION with wParam FALSE, and a second end of session
       while the first one runs answers zero. */
    {
        U session_class[10] = {0, (U)session_proc, 0, 0, 0, 0, 0, 0, 0, (U)"User32Session98"};
        U session;
        check(RegisterClassA(session_class) != 0, 14150);
        session = CreateWindowExA(0, "User32Session98", "session", 0x00cf0000,
                                  300, 60, 120, 80, 0, 0, 0, 0);
        check(session != 0, 14151);
        ShowWindow(session, 5);
        /* EWX_LOGOFF: neither bit 0 nor bit 1, so Windows 98 sets bit 31. */
        session_veto = 1; session_queries = 0; session_ends = 0;
        session_lparam = 0; session_wparam = 0xffff;
        check(ExitWindowsEx(0, 0) == 0, 14152);
        check(session_queries == 1 && session_lparam == 0x80000000, 14153);
        check(session_ends == 1 && session_wparam == 0, 14154);
        /* EWX_SHUTDOWN: bit 0 keeps the lParam as it came in. */
        session_veto = 0; session_queries = 0; session_ends = 0;
        session_lparam = 0; session_wparam = 0xffff;
        check(ExitWindowsEx(1, 0) == 1, 14155);
        check(session_queries == 1 && session_lparam == 1, 14156);
        check(session_ends == 1 && session_wparam == 1, 14157);
        /* USER data word 0B8h is set now; refuses the next call. */
        session_queries = 0; session_ends = 0;
        check(ExitWindowsEx(2, 0) == 0, 14158);
        check(session_queries == 0 && session_ends == 0, 14159);
        check(DestroyWindow(session) != 0, 14160);
    }

    /* ChangeMenuA: USER rewrites into the four menu calls. */
    {
        U menu = CreateMenu();
        char label[32];
        check(menu != 0, 14200);
        /* MF_APPEND (100h) goes to InsertMenu at the end. */
        check(ChangeMenuA(menu, 0, "First", 101, 0x100) != 0, 14201);
        check(ChangeMenuA(menu, 0, "Second", 102, 0x100) != 0, 14202);
        check(GetMenuItemCount(menu) == 2, 14203);
        check(GetMenuItemID(menu, 1) == 102, 14204);
        /* MF_CHANGE (80h) goes to ModifyMenu; Windows 98 keeps only 4C7Fh, so the
           position bit survives and the action bit does not. */
        check(ChangeMenuA(menu, 1, "Changed", 103, 0x80 | 0x400) != 0, 14205);
        check(GetMenuStringA(menu, 1, label, sizeof label, 0x400) == 7, 14206);
        check(same(label, "Changed"), 14207);
        check(GetMenuItemID(menu, 1) == 103, 14208);
        /* A null item text means a separator. */
        check(ChangeMenuA(menu, 0, 0, 0, 0x100) != 0, 14209);
        check((GetMenuState(menu, 2, 0x400) & 0x800) != 0, 14210);
        /* MF_REMOVE (1000h) always names the item by position. */
        check(ChangeMenuA(menu, 2, 0, 0, 0x1000) != 0, 14211);
        check(GetMenuItemCount(menu) == 2, 14212);
        /* MF_DELETE (200h) goes to DeleteMenu, which is by command here. */
        check(ChangeMenuA(menu, 103, 0, 0, 0x200) != 0, 14213);
        check(GetMenuItemCount(menu) == 1, 14214);
        /* No action bit selects insertion. */
        check(ChangeMenuA(menu, 0, "Inserted", 104, 0x400) != 0, 14215);
        check(GetMenuItemID(menu, 0) == 104, 14216);
        check(DestroyMenu(menu) != 0, 14217);
    }

    /* LoadMenuIndirectA: USER reads a version word and a header length
       and hands the rest to the same reader LoadMenu uses. */
    {
        static const unsigned short classic[] = {
            0, 0,                                   /* version 0, header 0 */
            0x0000, 200, 'O', 'n', 'e', 0,          /* item, id 200 */
            0x0010, 'S', 'u', 'b', 0,               /* MF_POPUP */
            0x0080, 201, 'I', 'n', 0,               /* MF_END inside the popup */
            0x0080, 202, 'T', 'w', 'o', 0};         /* MF_END */
        U menu = LoadMenuIndirectA(classic);
        char label[32];
        check(menu != 0, 14220);
        check(GetMenuItemCount(menu) == 3, 14221);
        check(GetMenuItemID(menu, 0) == 200, 14222);
        check(GetMenuStringA(menu, 0, label, sizeof label, 0x400) == 3 && same(label, "One"), 14223);
        check((GetMenuState(menu, 1, 0x400) & 0x10) != 0, 14224);
        check(GetMenuItemID(menu, 2) == 202, 14225);
        check(DestroyMenu(menu) != 0, 14226);
        /* a version the reader does not know answers null. */
        {
            static const unsigned short wrong[] = {2, 0, 0x0080, 1, 'X', 0};
            check(LoadMenuIndirectA(wrong) == 0, 14227);
        }
        SetLastError(0);
        check(LoadMenuIndirectA(0) == 0 && GetLastError() == 87, 14228);
    }

    /* FlashWindowEx: USER checks the record, is the machine. */
    {
        FlashInfo info;
        info.size = sizeof info; info.hwnd = parent; info.flags = 1; info.count = 0; info.timeout = 0;
        /* a wrong size is a parameter error. */
        info.size = 12;
        SetLastError(0);
        check(FlashWindowEx(&info) == 0 && GetLastError() == 87, 14230);
        info.size = sizeof info;
        /* a bit outside the accepted mask is one too. */
        info.flags = 0x100;
        SetLastError(0);
        check(FlashWindowEx(&info) == 0 && GetLastError() == 87, 14231);
        /* Windows 98 leaves the window that owns the foreground alone, so the
           machine is driven on a window that cannot be the foreground. */
        info.hwnd = child;
        /* The first flash answers the state the caption had on entry. */
        info.flags = 1;
        check(FlashWindowEx(&info) == 0, 14232);
        /* The second one answers the state the first one left behind. */
        check(FlashWindowEx(&info) == 1, 14233);
        check(FlashWindowEx(&info) == 0, 14234);
        /* FLASHW_STOP puts the caption back and forgets the window, so the
           next call starts from zero again. */
        info.flags = 0;
        check(FlashWindowEx(&info) == 1, 14235);
        info.flags = 1;
        check(FlashWindowEx(&info) == 0, 14237);
        info.flags = 0;
        check(FlashWindowEx(&info) == 1, 14238);
        info.hwnd = 0;
        info.flags = 1;
        SetLastError(0);
        check(FlashWindowEx(&info) == 0 && GetLastError() == 1400, 14236);
    }

    /* RegisterDeviceNotificationA: USER32 validates and then needs a
       device node NEO98 does not have. */
    {
        DeviceFilter filter;
        U handle;
        unsigned i;
        for (i = 0; i < 16; ++i) filter.guid[i] = (unsigned char)i;
        /* wants at least 1Dh bytes: the twelve of the header, the
           sixteen of the class and the first byte of the name. */
        filter.size = 29; filter.type = 5; filter.reserved = 0; filter.name[0] = 0;
        SetLastError(0);
        check(RegisterDeviceNotificationA(0, &filter, 0) == 0 && GetLastError() == 87, 14240);
        SetLastError(0);
        check(RegisterDeviceNotificationA(parent, 0, 0) == 0 && GetLastError() == 87, 14241);
        SetLastError(0);
        check(RegisterDeviceNotificationA(parent, &filter, 1) == 0 && GetLastError() == 87, 14242);
        filter.type = 2;
        SetLastError(0);
        check(RegisterDeviceNotificationA(parent, &filter, 0) == 0 && GetLastError() == 13, 14243);
        filter.type = 6;
        SetLastError(0);
        check(RegisterDeviceNotificationA(parent, &filter, 0) == 0 && GetLastError() == 13, 14244);
        filter.type = 5;
        handle = RegisterDeviceNotificationA(parent, &filter, 0);
        check(handle != 0, 14245);
        check(UnregisterDeviceNotification(handle) != 0, 14246);
        SetLastError(0);
        check(UnregisterDeviceNotification(0) == 0 && GetLastError() == 87, 14247);
        SetLastError(0);
        check(UnregisterDeviceNotification((U)&filter) == 0 && GetLastError() == 13, 14248);
    }

    /* EndTask: USER. Without a window and without a task the answer is
       2, and the flag that forbids a user interface keeps the box away. */
    check(EndTask(0, 0, 0, 4) == 2, 14250);

    /* The MDI: the MDICLIENT class, DefFrameProc, DefMDIChildProc and
       TranslateMDISysAccel of USER.EXE segment 17. */
    {
        U frame_class[10] = {0, (U)frame_proc, 0, 0, 0, 0, 0, 0, 0, (U)"User32Frame98"};
        U child_class[10] = {0, (U)child_proc, 0, 0, 0, 0, 0, 0, 0, (U)"User32Child98"};
        U frame, first, second, menu, window_menu, defaulted, zeroed;
        int box[4], other[4];
        ClientCreate client;
        MdiCreate create;
        Message key;
        char title[128], label[64];
        check(RegisterClassA(frame_class) != 0, 14260);
        check(RegisterClassA(child_class) != 0, 14261);
        frame = CreateWindowExA(0, "User32Frame98", "Frame", 0x00cf0000, 20, 20, 400, 300, 0, 0, 0, 0);
        check(frame != 0, 14262);
        ShowWindow(frame, 5);
        menu = CreateMenu();
        window_menu = CreatePopupMenu();
        check(menu != 0 && window_menu != 0, 14263);
        check(AppendMenuA(menu, 0x10, window_menu, "&Window") != 0, 14264);
        check(SetMenu(frame, menu) != 0, 14265);
        client.menu = window_menu;
        client.first = 0xff00;
        mdi_client = CreateWindowExA(0, "MDICLIENT", 0, 0x40000000 | 0x10000000,
                                     0, 0, 380, 260, frame, 0, 0, (U)&client);
        check(mdi_client != 0, 14266);
        create.klass = (U)"User32Child98"; create.instance = 0;
        create.x = 0; create.y = 0; create.cx = 120; create.cy = 90;
        create.style = 0; create.param = 0;
        create.title = (U)"Alpha";
        first = SendMessageA(mdi_client, 0x0220, 0, (U)&create);
        check(first != 0, 14267);
        create.title = (U)"Beta";
        second = SendMessageA(mdi_client, 0x0220, 0, (U)&create);
        check(second != 0 && second != first, 14268);
        /* lists the children behind a separator, "&1 Alpha" first. */
        check(GetMenuItemCount(window_menu) == 3, 14269);
        check((GetMenuState(window_menu, 0, 0x400) & 0x800) != 0, 14270);
        check(GetMenuItemID(window_menu, 1) == 0xff00, 14271);
        check(GetMenuStringA(window_menu, 1, label, sizeof label, 0x400) == 8, 14272);
        check(same(label, "&1 Alpha"), 14273);
        check(GetMenuStringA(window_menu, 2, label, sizeof label, 0x400) == 7, 14274);
        check(same(label, "&2 Beta"), 14275);
        /* The second child is the active one; WM_MDIGETACTIVE says so. */
        check(SendMessageA(mdi_client, 0x0229, 0, 0) == second, 14276);
        /* WM_COMMAND with the first list id activates the first child. */
        SendMessageA(frame, 0x0111, 0xff00, 0);
        check(SendMessageA(mdi_client, 0x0229, 0, 0) == first, 14277);
        /* WM_MDINEXT walks on to the other child. */
        SendMessageA(mdi_client, 0x0224, 0, 0);
        check(SendMessageA(mdi_client, 0x0229, 0, 0) == second, 14278);
        /* Maximizing composes the frame title as "Frame - [Beta]". */
        SendMessageA(mdi_client, 0x0225, second, 0);
        check(GetWindowTextA(frame, title, sizeof title) == 14, 14279);
        check(same(title, "Frame - [Beta]"), 14280);
        /* Restoring gives the frame its own title back. */
        SendMessageA(mdi_client, 0x0223, second, 0);
        check(GetWindowTextA(frame, title, sizeof title) == 5, 14281);
        check(same(title, "Frame"), 14282);
        /* TranslateMDISysAccel takes only a key press and only with CTRL. */
        key.hwnd = frame; key.message = 0x0100; key.wp = 0x75; key.lp = 0;
        key.time = 0; key.x = 0; key.y = 0;
        check(TranslateMDISysAccel(mdi_client, &key) == 0, 14283);
        key.message = 0x0200;
        check(TranslateMDISysAccel(mdi_client, &key) == 0, 14284);
        /* MDI children use default dimensions for both CW_USEDEFAULT and zero, and default x/y select the next cascade position. File Manager depends on a zero height requesting the default. */
        create.title = (U)"Gamma";
        create.x = 0x80000000; create.y = 0;
        create.cx = 0x80000000; create.cy = 0x80000000;
        defaulted = SendMessageA(mdi_client, 0x0220, 0, (U)&create);
        check(defaulted != 0 && GetWindowRect(defaulted, box) != 0, 14289);
        check(box[2] - box[0] > 0 && box[3] - box[1] > 0, 14290);
        SendMessageA(mdi_client, 0x0221, defaulted, 0);
        create.title = (U)"Delta";
        create.x = 0x80000000; create.y = 0; create.cx = 0; create.cy = 0;
        zeroed = SendMessageA(mdi_client, 0x0220, 0, (U)&create);
        check(zeroed != 0 && GetWindowRect(zeroed, other) != 0, 14291);
        check(other[2] - other[0] == box[2] - box[0], 14292);
        check(other[3] - other[1] == box[3] - box[1], 14293);
        /* USER counts every child created, including destroyed ones; Delta advances one stair beyond Gamma. */
        check(other[0] == box[0] + GetSystemMetrics(30) + GetSystemMetrics(32) &&
              other[1] == box[1] + GetSystemMetrics(31) + GetSystemMetrics(33), 14294);
        SendMessageA(mdi_client, 0x0221, zeroed, 0);
        /* WM_MDIDESTROY takes the child out of the menu again. */
        SendMessageA(mdi_client, 0x0221, second, 0);
        check(IsWindowVisible(second) == 0, 14285);
        check(GetMenuItemCount(window_menu) == 2, 14286);
        check(SendMessageA(mdi_client, 0x0229, 0, 0) == first, 14287);
        check(DestroyWindow(frame) != 0, 14288);
        mdi_client = 0;
    }


    /* AnimateWindow: USER over the worker. NEO98 draws no
       animation, so the test asks for the decisions and the end state. */
    {
        U animated = CreateWindowExA(0, "User32Imports98", "animate", 0x00cf0000,
                                     60, 70, 120, 90, 0, 0, 0, 0);
        U shaped;
        check(animated != 0, 14300);
        check(IsWindowVisible(animated) == 0, 14301);
        /* neither a direction nor AW_BLEND leaves nothing to do. */
        check(AnimateWindow(animated, 100, 0) == 0, 14302);
        check(AnimateWindow(animated, 100, 0x00020000) == 0, 14303);
        /* Win98 has no layered window, so a blended hide fails. */
        check(AnimateWindow(animated, 100, 0x00090000) == 0, 14304);
        /* hiding a window that is already hidden fails. */
        check(AnimateWindow(animated, 100, 0x00010001) == 0, 14305);
        /* The show leaves the window visible and answers one. */
        check(AnimateWindow(animated, 100, 0x00000001) == 1, 14306);
        check(IsWindowVisible(animated) == 1, 14307);
        /* Showing it a second time fails, the window stays visible. */
        check(AnimateWindow(animated, 100, 0x00000001) == 0, 14308);
        check(IsWindowVisible(animated) == 1, 14309);
        /* the hide leaves the window hidden and answers one. */
        check(AnimateWindow(animated, 100, 0x00010004) == 1, 14310);
        check(IsWindowVisible(animated) == 0, 14311);
        /* AW_ACTIVATE shows and takes the activation. */
        check(AnimateWindow(animated, 0, 0x00020010) == 1, 14312);
        check(IsWindowVisible(animated) == 1, 14313);
        check(AnimateWindow(animated, 0, 0x00010010) == 1, 14314);
        /* a window that already carries a region fails, because the
           animation needs that field for itself. The blend path does not read
           it, and NEO98 answers that one the way Win98 does. */
        shaped = CreateRectRgn(0, 0, 40, 40);
        check(shaped != 0 && SetWindowRgn(animated, shaped, 0) != 0, 14315);
        check(AnimateWindow(animated, 100, 0x00000001) == 0, 14316);
        check(IsWindowVisible(animated) == 0, 14317);
        check(SetWindowRgn(animated, 0, 0) != 0, 14318);
        check(AnimateWindow(animated, 100, 0x00000001) == 1, 14319);
        /* USER derives a small icon from a big one when the
           window carries none, so WM_GETICON answers both forms. */
        {
            U big = LoadIconA(0, (const char *)32512);
            check(big != 0, 14321);
            check(SendMessageA(animated, 0x0080, 1, big) == 0, 14322);
            check(SendMessageA(animated, 0x007f, 1, 0) == big, 14323);
            check(SendMessageA(animated, 0x007f, 0, 0) != 0, 14324);
            check(SendMessageA(animated, 0x007f, 0, 0) != big, 14325);
        }
        check(DestroyWindow(animated) != 0, 14320);
    }


    /* The nine information queries of USER.EXE segment 28. Every one of them
       rejects a wrong cbSize, and none of them touches the last error. */
    {
        WindowInfo wi;
        TitleBarInfo ti;
        ScrollBarInfo sbi;
        CursorInfo ci;
        GuiThreadInfo gti;
        AltTabInfo ati;
        char klass[64];
        U bars = CreateWindowExA(0, "User32Imports98", "bars", 0x00cf0000 | 0x00300000,
                                 30, 40, 220, 160, 0, 0, 0, 0);
        check(bars != 0, 14330);
        ShowWindow(bars, 5);

        /* GetWindowInfo: the two rectangles in screen coordinates, both style
           words, the class atom and the frame width. Windows 98 rejects a cbSize
           that is not sizeof(WINDOWINFO). */
        wi.cbSize = sizeof wi - 1;
        SetLastError(12345);
        check(GetWindowInfo(bars, &wi) == 0, 14331);
        check(GetLastError() == 12345, 14332);
        check(GetWindowInfo(0, &wi) == 0, 14333);
        wi.cbSize = sizeof wi;
        check(GetWindowInfo(bars, &wi) != 0, 14334);
        check(wi.rcWindow.right - wi.rcWindow.left == 220, 14335);
        check(wi.rcWindow.bottom - wi.rcWindow.top == 160, 14336);
        check(wi.rcClient.left >= wi.rcWindow.left && wi.rcClient.right <= wi.rcWindow.right, 14337);
        check(wi.rcClient.top >= wi.rcWindow.top && wi.rcClient.bottom <= wi.rcWindow.bottom, 14338);
        check((wi.dwStyle & 0x00cf0000) == 0x00cf0000, 14339);
        /* WS_EX_WINDOWEDGE two, the border one, WS_THICKFRAME one. */
        check(wi.cxWindowBorders == 4 * GetSystemMetrics(5), 14340);
        check(wi.cyWindowBorders == 4 * GetSystemMetrics(6), 14341);
        check(wi.atom != 0, 14342);

        /* GetTitleBarInfo: the band and the six states. */
        ti.cbSize = 4;
        check(GetTitleBarInfo(bars, &ti) == 0, 14343);
        ti.cbSize = sizeof ti;
        check(GetTitleBarInfo(bars, &ti) != 0, 14344);
        check((ti.rgstate[0] & 0x00100000) != 0, 14345);       /* FOCUSABLE */
        check((ti.rgstate[0] & 0x00008000) == 0, 14346);       /* not INVISIBLE */
        check(ti.rgstate[1] == 0x00008000, 14347);             /* the reserved slot */
        check(ti.rcTitleBar.bottom > ti.rcTitleBar.top, 14348);
        check(ti.rcTitleBar.top >= wi.rcWindow.top, 14349);
        /* WS_MINIMIZEBOX and WS_MAXIMIZEBOX are both there, so neither box is
           greyed out; the help button is invisible beside them. */
        check(ti.rgstate[2] == 0, 14350);
        check(ti.rgstate[3] == 0, 14351);
        check(ti.rgstate[4] == 0x00008000, 14352);
        /* A window without a caption reports an invisible bar. */
        check(GetTitleBarInfo(child, &ti) != 0, 14353);
        check((ti.rgstate[0] & 0x00008000) != 0, 14354);

        /* GetScrollBarInfo: the window carries both bars. */
        sbi.cbSize = 8;
        check(GetScrollBarInfo(bars, (U)-5, &sbi) == 0, 14355);
        sbi.cbSize = sizeof sbi;
        SetScrollRange(bars, 1, 0, 100, 0);
        check(GetScrollBarInfo(bars, (U)-5, &sbi) != 0, 14356);
        check((sbi.rgstate[0] & 0x00008000) == 0, 14357);
        check(sbi.rcScrollBar.right > sbi.rcScrollBar.left, 14358);
        check(sbi.dxyLineButton > 0, 14359);
        check(sbi.xyThumbBottom > sbi.xyThumbTop, 14360);
        /* A window without the style reports an invisible bar and nothing else. */
        check(GetScrollBarInfo(parent, (U)-5, &sbi) != 0, 14361);
        check(sbi.rgstate[0] == 0x00008000, 14362);

        /* GetCursorInfo: the cursor the display shows and where it is. */
        ci.cbSize = 4;
        check(GetCursorInfo(&ci) == 0, 14363);
        ci.cbSize = sizeof ci;
        check(GetCursorInfo(&ci) != 0, 14364);
        check(ci.flags == 0 || ci.flags == 1, 14365);

        /* GetGUIThreadInfo: this thread, with no menu and no move loop. */
        gti.cbSize = 4;
        check(GetGUIThreadInfo(0, &gti) == 0, 14366);
        gti.cbSize = sizeof gti;
        check(GetGUIThreadInfo(0, &gti) != 0, 14367);
        check((gti.flags & 6) == 0, 14368);
        check(gti.hwndMoveSize == 0 && gti.hwndMenuOwner == 0, 14369);
        check(GetGUIThreadInfo(0x7ffffffe, &gti) == 0, 14370);

        /* GetAltTabInfo answers only while USER's own switcher is up. */
        ati.cbSize = sizeof ati;
        check(GetAltTabInfo(bars, -1, &ati, 0, 0) == 0, 14371);

        /* RealGetWindowClass answers what GetClassName answers for a class
           that no built-in control superclassed. */
        klass[0] = 0;
        check(RealGetWindowClass(bars, klass, sizeof klass) != 0, 14372);
        check(same(klass, "User32Imports98"), 14373);
        check(RealGetWindowClass(0, klass, sizeof klass) == 0, 14374);

        /* GetListBoxInfo and GetComboBoxInfo over real controls. */
        {
            ComboBoxInfo cbi;
            U list = CreateWindowExA(0, "LISTBOX", "", 0x50000000, 4, 4, 100, 60,
                                     bars, 0, 0, 0);
            U combo = CreateWindowExA(0, "COMBOBOX", "", 0x50000002, 4, 70, 100, 80,
                                      bars, 0, 0, 0);
            check(list != 0 && combo != 0, 14375);
            check(GetListBoxInfo(list) == 0, 14376);
            SendMessageA(list, 0x0180, 0, (U)"one");    /* LB_ADDSTRING */
            SendMessageA(list, 0x0180, 0, (U)"two");
            check(GetListBoxInfo(list) == 2, 14377);
            check(GetListBoxInfo(bars) == 0, 14378);
            cbi.cbSize = 8;
            check(GetComboBoxInfo(combo, &cbi) == 0, 14379);
            cbi.cbSize = sizeof cbi;
            check(GetComboBoxInfo(combo, &cbi) != 0, 14380);
            check(cbi.hwndCombo == combo, 14381);
            check(cbi.hwndList != 0, 14382);
            check(cbi.rcButton.right > cbi.rcButton.left, 14383);
            /* Windows 98 takes the combo's own list box as well. */
            check(GetComboBoxInfo(cbi.hwndList, &cbi) != 0, 14384);
            check(cbi.hwndCombo == combo, 14385);
            check(GetComboBoxInfo(list, &cbi) == 0, 14386);
            check(DestroyWindow(combo) != 0, 14387);
            check(DestroyWindow(list) != 0, 14388);
        }
        check(DestroyWindow(bars) != 0, 14389);
    }


    /* SetSysColors, SetSysColorsTemp, HiliteMenuItem, CreateMDIWindowA, the
       two DlgDirSelectEx entries and the two EnumProps entries. */
    {
        int indices[2];
        U colours[2], saved[2], brushes[2], cookie, brush, menu;
        char picked[64];
        U list;

        /* SetSysColors recolours the brush in place, so the handle stays. */
        brush = GetSysColorBrush(15);              /* COLOR_BTNFACE */
        check(brush != 0, 14400);
        saved[0] = GetSysColor(15);
        saved[1] = GetSysColor(13);                /* COLOR_HIGHLIGHT */
        indices[0] = 15; indices[1] = 13;
        colours[0] = 0x00203040; colours[1] = 0x00506070;
        check(SetSysColors(2, indices, colours) != 0, 14401);
        check(GetSysColor(15) == 0x00203040, 14402);
        check(GetSysColor(13) == 0x00506070, 14403);
        check(GetSysColorBrush(15) == brush, 14404);
        /* An index above 28 is dropped and the call still answers TRUE. */
        indices[0] = 29; colours[0] = 0x00010203;
        check(SetSysColors(1, indices, colours) != 0, 14405);
        check(GetSysColor(15) == 0x00203040, 14406);
        /* Put the two back. */
        indices[0] = 15; indices[1] = 13;
        colours[0] = saved[0]; colours[1] = saved[1];
        check(SetSysColors(2, indices, colours) != 0, 14407);
        check(GetSysColor(15) == saved[0], 14408);

        /* SetSysColorsTemp swaps the table and hands back a cookie. */
        colours[0] = 0x00112233; colours[1] = 0x00445566;
        brushes[0] = brush; brushes[1] = brush;
        check(SetSysColorsTemp(colours, brushes, 30) == 0, 14409);
        cookie = SetSysColorsTemp(colours, brushes, 2);
        check(cookie != 0, 14410);
        check(GetSysColor(0) == 0x00112233, 14411);
        check(SetSysColorsTemp(0, 0, cookie) != 0, 14412);
        check(GetSysColor(0) != 0x00112233, 14413);
        check(SetSysColorsTemp(0, 0, cookie) == 0, 14414);

        /* HiliteMenuItem sets and clears the one MF_HILITE bit. */
        menu = CreatePopupMenu();
        check(menu != 0, 14415);
        check(AppendMenuA(menu, 0, 501, "Item") != 0, 14416);
        check(HiliteMenuItem(parent, menu, 501, 0x0080) != 0, 14417);
        check((GetMenuState(menu, 501, 0) & 0x0080) != 0, 14418);
        check(HiliteMenuItem(parent, menu, 501, 0) != 0, 14419);
        check((GetMenuState(menu, 501, 0) & 0x0080) == 0, 14420);
        /* refuses a flag word with 1000h, 2000h or 8000h set. */
        check(HiliteMenuItem(parent, menu, 501, 0x1080) == 0, 14421);
        check(DestroyMenu(menu) != 0, 14422);

        /* CreateMDIWindowA is CreateWindowEx with WS_EX_MDICHILD in front. */
        {
            U mdi = CreateMDIWindowA("User32Imports98", "mdi", 0x40000000,
                                     5, 6, 40, 30, parent, 0, 0x1234);
            check(mdi != 0, 14423);
            check(GetParent(mdi) == parent, 14424);
            check(DestroyWindow(mdi) != 0, 14425);
        }

        /* DlgDirSelectExA over a list the directory listing filled. */
        list = CreateWindowExA(0, "LISTBOX", "", 0x50200002, 0, 0, 120, 80,
                               parent, 2002, 0, 0);
        check(list != 0, 14426);
        SendMessageA(list, 0x184, 0, 0);
        SendMessageA(list, 0x18d, 0xd000, (U)"*.*");       /* drives only */
        check(SendMessageA(list, 0x18b, 0, 0) > 0, 14427);
        SendMessageA(list, 0x186, 0, 0);                   /* LB_SETCURSEL 0 */
        picked[0] = 0;
        /* A drive is [-x-] and comes back as x: with the answer TRUE. */
        check(DlgDirSelectExA(parent, picked, sizeof picked, 2002) != 0, 14428);
        check(picked[1] == ':' && picked[2] == 0, 14429);
        /* Nothing selected answers FALSE. */
        SendMessageA(list, 0x186, (U)-1, 0);
        check(DlgDirSelectExA(parent, picked, sizeof picked, 2002) == 0, 14430);
        /* A plain name gets a trailing dot. */
        SendMessageA(list, 0x184, 0, 0);
        SendMessageA(list, 0x180, 0, (U)"README");
        SendMessageA(list, 0x186, 0, 0);
        check(DlgDirSelectExA(parent, picked, sizeof picked, 2002) == 0, 14431);
        check(same(picked, "README."), 14432);
        /* A directory keeps its name and gains a backslash. */
        SendMessageA(list, 0x184, 0, 0);
        SendMessageA(list, 0x180, 0, (U)"[WINDOWS]");
        SendMessageA(list, 0x186, 0, 0);
        check(DlgDirSelectExA(parent, picked, sizeof picked, 2002) != 0, 14433);
        check(same(picked, "WINDOWS\\"), 14434);
        check(DlgDirSelectExA(parent, picked, sizeof picked, 2999) == 0, 14435);
        check(DestroyWindow(list) != 0, 14436);

        /* EnumPropsA and EnumPropsExA. */
        prop_seen = 0; prop_stop = 0; prop_last_data = 0; prop_parameter = 0;
        check(EnumPropsA(parent, (U)prop_enum) == 0xffffffff, 14437);
        check(SetPropA(parent, "alpha", 0x1111) != 0, 14438);
        check(SetPropA(parent, "beta", 0x2222) != 0, 14439);
        check(EnumPropsA(parent, (U)prop_enum) == 1, 14440);
        check(prop_seen == 2, 14441);
        check(prop_last_data == 0x2222, 14442);
        prop_seen = 0; prop_stop = 1;
        check(EnumPropsA(parent, (U)prop_enum) == 0, 14443);
        check(prop_seen == 1, 14444);
        prop_seen = 0; prop_stop = 0;
        check(EnumPropsExA(parent, (U)prop_enum_ex, 0xabcd) == 1, 14445);
        check(prop_seen == 2 && prop_parameter == 0xabcd, 14446);
        check(RemovePropA(parent, "alpha") == 0x1111, 14447);
        check(RemovePropA(parent, "beta") == 0x2222, 14448);
        check(EnumPropsA(parent, (U)prop_enum) == 0xffffffff, 14449);
    }

    check(DestroyWindow(parent) != 0, 14098);
}
