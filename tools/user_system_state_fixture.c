/* SPDX-License-Identifier: GPL-2.0-or-later */
/* USER and GDI state that depends on the installed Win98 tree, run by
   tools/test_user_system_state_guest.py with the tree mounted. Exits with the
   code of the first failed check, zero when all pass. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U SystemParametersInfoA(U, U, void *, U);
API void SetLastError(U);
API U GetLastError(void);
API void ExitProcess(U);
API U SwapMouseButton(U);
API U IsZoomed(U);
API int GetSystemMetrics(int);
API U GetAsyncKeyState(int);
API U GetKeyState(int);
API void mouse_event(U, U, U, U, U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U DestroyWindow(U);
API U GetModuleHandleA(const char *);
API void Sleep(U);
API U LoadLibraryA(const char *);
API U LoadImageA(U, const char *, U, int, int, U);
API int GetObjectA(U, int, void *);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U GetPixel(U, int, int);
API U DeleteDC(U);
API U DeleteObject(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void fill(unsigned char *out, U count)
{
    while (count--)
        *out++ = 0xcd;
}
static U value(const unsigned char *p) { return p[0] | (U)p[1] << 8 | (U)p[2] << 16 | (U)p[3] << 24; }
static void font_smoothing(void)
{
    unsigned char data[16];
    /* The installation names cool.dll under
       HKLM\Software\Microsoft\Plus!\Setup, so USER keeps the Plus
       module and SPI_GETDRAGFULLWINDOWS answers with the switch,
       which is 0 here. */
    fill(data, sizeof data);
    check(SystemParametersInfoA(38, 0, data + 4, 0) == 1, 100);
    check(value(data) == 0xcdcdcdcd && value(data + 4) == 0 && value(data + 8) == 0xcdcdcdcd, 101);
    /* USER hands 4Ah and 4Bh to GDI.528. WIN.INI has no
       [TrueType] FontSmoothing, so bit 10h of GDI DS:34h starts clear. */
    fill(data, sizeof data);
    SetLastError(9898);
    check(SystemParametersInfoA(0x4a, 0, data + 4, 0) == 1, 102);
    check(value(data + 4) == 0 && value(data + 8) == 0xcdcdcdcd && GetLastError() == 9898, 103);
    check(SystemParametersInfoA(0x4b, 1, 0, 0) == 1, 104);
    check(SystemParametersInfoA(0x4a, 0, data + 4, 0) == 1 && value(data + 4) == 0x10, 105);
    check(SystemParametersInfoA(0x4b, 0, 0, 0) == 1, 106);
    check(SystemParametersInfoA(0x4a, 0, data + 4, 0) == 1 && value(data + 4) == 0, 107);
}
/* SwapMouseButton and what it does to the input. */
static U seen[8], seen_count, key_state_left;
static U __stdcall record(U hwnd, U message, U wp, U lp)
{
    if (message >= 0x201 && message <= 0x205 && seen_count < 8)
    {
        seen[seen_count++] = message;
        if (message == 0x201 || message == 0x204)
            key_state_left = GetKeyState(1) & 0x8000;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}
static void pump(void)
{
    U msg[7];
    for (U i = 0; i < 20; ++i)
    {
        while (PeekMessageA(msg, 0, 0, 0, 1))
            DispatchMessageA(msg);
        Sleep(1);
    }
}
static void click(U window, U base)
{
    seen_count = 0;
    key_state_left = 0;
    mouse_event(2, 0, 0, 0, 0); /* MOUSEEVENTF_LEFTDOWN */
    pump();
    /* GetAsyncKeyState reports the physical button. */
    check((GetAsyncKeyState(1) & 0x8000) != 0, base);
    mouse_event(4, 0, 0, 0, 0); /* MOUSEEVENTF_LEFTUP */
    pump();
    (void)window;
}
static void swap_buttons(void)
{
    U klass[10] = {0, (U)record, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)"SwapProbe"};
    check(RegisterClassA(klass) != 0, 200);
    U window = CreateWindowExA(0, "SwapProbe", "", 0x90000000u, 100, 100, 200, 200, 0, 0, 0, 0);
    check(window != 0, 201);
    /* MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE to 200,200 of 800x600. */
    mouse_event(0x8001, 200 * 65536 / 800, 200 * 65536 / 600, 0, 0);
    pump();
    check(GetSystemMetrics(23) == 0, 202);
    /* Windows 98 answers the previous flag and stores the new one as SM_SWAPBUTTON. */
    check(SwapMouseButton(1) == 0, 203);
    check(GetSystemMetrics(23) == 1, 204);
    click(window, 205);
    check(seen_count == 2 && seen[0] == 0x204 && seen[1] == 0x205, 206);
    /* The queue state follows the messages: the pressed button is the right one. */
    check(key_state_left == 0, 207);
    check(SwapMouseButton(0) == 1, 208);
    check(GetSystemMetrics(23) == 0, 209);
    click(window, 210);
    check(seen_count == 2 && seen[0] == 0x201 && seen[1] == 0x202, 211);
    check(key_state_left != 0, 212);
    /* SPI_SETMOUSEBUTTONSWAP goes through the same flag. */
    check(SystemParametersInfoA(33, 1, 0, 0) == 1 && GetSystemMetrics(23) == 1, 213);
    check(SwapMouseButton(0) == 1 && GetSystemMetrics(23) == 0, 214);
    DestroyWindow(window);
}
/* LoadImageA with IMAGE_BITMAP and a size. */
static void sized_bitmap(void)
{
    const U desk = LoadLibraryA("DESK.CPL");
    check(desk != 0, 300);
    /* DESK.CPL asks for its monitor picture 1251 at 355 by 161. */
    U info[6] = {0};
    const U asked = LoadImageA(desk, (const char *)1251, 0, 355, 161, 0);
    check(asked != 0, 301);
    check(GetObjectA(asked, 24, info) == 24 && info[1] == 355 && info[2] == 161, 302);
    DeleteObject(asked);
    /* Twice the size keeps every source pixel in the matching 2x2 cell. */
    const U native = LoadImageA(desk, (const char *)1251, 0, 0, 0, 0);
    check(native != 0 && GetObjectA(native, 24, info) == 24, 303);
    const int w = (int)info[1], h = (int)info[2];
    const U doubled = LoadImageA(desk, (const char *)1251, 0, 2 * w, 0, 0);
    check(doubled != 0 && GetObjectA(doubled, 24, info) == 24 && (int)info[1] == 2 * w && (int)info[2] == h, 304);
    DeleteObject(doubled);
    const U both = LoadImageA(desk, (const char *)1251, 0, 2 * w, 2 * h, 0);
    check(both != 0, 305);
    const U a = CreateCompatibleDC(0), b = CreateCompatibleDC(0);
    const U old_a = SelectObject(a, native), old_b = SelectObject(b, both);
    U differing = 0;
    for (int y = 0; y < h; y += 7)
        for (int x = 0; x < w; x += 5)
            if (GetPixel(a, x, y) != GetPixel(b, 2 * x + 1, 2 * y + 1)) ++differing;
    check(differing == 0, 306);
    /* LR_CREATEDIBSECTION with a size: patches the header,
       stretches the DIB into the section. */
    const U section = LoadImageA(desk, (const char *)1251, 0, 2 * w, 2 * h, 0x2000);
    check(section != 0, 307);
    check(GetObjectA(section, 24, info) == 24 && (int)info[1] == 2 * w && (int)info[2] == 2 * h, 309);
    check(info[5] != 0, 310);
    SelectObject(b, section);
    differing = 0;
    for (int y = 0; y < h; y += 7)
        for (int x = 0; x < w; x += 5)
            if (GetPixel(a, x, y) != GetPixel(b, 2 * x, 2 * y)) ++differing;
    check(differing == 0, 308);
    SelectObject(a, old_a);
    SelectObject(b, old_b);
    DeleteDC(a);
    DeleteDC(b);
    DeleteObject(section);
    DeleteObject(both);
    DeleteObject(native);
}
/* the middle button outside the client area. */
static U middle_count, middle_message[8], middle_wp[8], middle_lp[8];
static U move_message, move_wp, move_lp;
static U __stdcall middle_record(U hwnd, U message, U wp, U lp)
{
    if (message == 0xa0 || message == 0x200)
    {
        move_message = message;
        move_wp = wp;
        move_lp = lp;
    }
    if (((message >= 0x201 && message <= 0x209) || (message >= 0xa1 && message <= 0xa9)) &&
        middle_count < 8)
    {
        middle_message[middle_count] = message;
        middle_wp[middle_count] = wp;
        middle_lp[middle_count++] = lp;
    }
    /* A right click on a frame would open the system menu and wait in its
       loop; the recorder only wants the button messages. */
    if (message == 0x7b)
        return 0;
    return DefWindowProcA(hwnd, message, wp, lp);
}
/* Presses and releases one button at x, y; x a multiple of 25 and y of 75
   keep the absolute coordinates exact on the 800 x 600 screen. */
static void button_at(U down, U up, U x, U y)
{
    mouse_event(0x8001 | down, x * 65536 / 800, y * 65536 / 600, 0, 0);
    pump();
    mouse_event(up, 0, 0, 0, 0);
    pump();
}
/* Queues press and release before pumping: a press on a caption enters the
   move loop of DefWindowProc, which must find the release already queued. */
static void click_queued(U down, U up, U x, U y)
{
    mouse_event(0x8001 | down, x * 65536 / 800, y * 65536 / 600, 0, 0);
    mouse_event(up, 0, 0, 0, 0);
    pump();
}
static int near(U lp, U x, U y)
{
    const int dx = (int)(lp & 0xffff) - (int)x, dy = (int)(lp >> 16) - (int)y;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
}
static void middle_button(void)
{
    U plain[10] = {0, (U)middle_record, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)"MiddlePlain"};
    U doubled[10] = {8, (U)middle_record, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, (U)"MiddleDouble"};
    check(RegisterClassA(plain) != 0 && RegisterClassA(doubled) != 0, 400);
    /* WS_OVERLAPPEDWINDOW | WS_VISIBLE; its caption covers y = 75. */
    const U framed = CreateWindowExA(0, "MiddlePlain", "Middle", 0x10cf0000u, 50, 65, 300, 200, 0, 0, 0, 0);
    const U client = CreateWindowExA(0, "MiddlePlain", "", 0x90000000u, 400, 75, 200, 150, 0, 0, 0, 0);
    const U clicks = CreateWindowExA(0, "MiddleDouble", "", 0x90000000u, 400, 300, 200, 150, 0, 0, 0, 0);
    check(framed && client && clicks, 401);
    /* On the caption: WM_NCMBUTTONDOWN and UP with HTCAPTION and the screen
       point, the second press inside the double-click time WM_NCMBUTTONDBLCLK
       even without CS_DBLCLKS. */
    middle_count = 0;
    button_at(0x20, 0x40, 150, 75);
    button_at(0x20, 0x40, 150, 75);
    check(middle_count == 4, 402);
    check(middle_message[0] == 0xa7 && middle_message[1] == 0xa8 && middle_message[2] == 0xa9 &&
          middle_message[3] == 0xa8, 403);
    check(middle_wp[0] == 2 && middle_wp[1] == 2 && middle_wp[2] == 2, 404);
    check(near(middle_lp[0], 150, 75) && near(middle_lp[2], 150, 75), 405);
    /* In a client area without CS_DBLCLKS the second press stays 0207h. */
    middle_count = 0;
    button_at(0x20, 0x40, 500, 150);
    button_at(0x20, 0x40, 500, 150);
    check(middle_count == 4 && middle_message[0] == 0x207 && middle_message[1] == 0x208 &&
          middle_message[2] == 0x207 && middle_message[3] == 0x208, 406);
    check(near(middle_lp[0], 100, 75), 407);
    /* With CS_DBLCLKS it becomes WM_MBUTTONDBLCLK; a left press before a
       middle press is no double click (Windows 98 compares the message). */
    middle_count = 0;
    button_at(0x20, 0x40, 500, 375);
    button_at(0x20, 0x40, 500, 375);
    button_at(2, 4, 500, 375);
    button_at(0x20, 0x40, 500, 375);
    check(middle_count == 8 && middle_message[0] == 0x207 && middle_message[2] == 0x209 &&
          middle_message[3] == 0x208 && middle_message[4] == 0x201 && middle_message[6] == 0x207, 408);
    DestroyWindow(clicks);
    DestroyWindow(client);
    DestroyWindow(framed);
}
/* double clicks outside the client area and with the right button. A double click on the caption maximizes, a second one
   restores; the right button yields 00A6h on the frame and 0206h in a
   CS_DBLCLKS client area. */
static void frame_double_click(void)
{
    const U framed = CreateWindowExA(0, "MiddlePlain", "Frame", 0x10cf0000u, 50, 65, 300, 200, 0, 0, 0, 0);
    const U clicks = CreateWindowExA(0, "MiddleDouble", "", 0x90000000u, 400, 300, 200, 150, 0, 0, 0, 0);
    check(framed && clicks, 500);
    middle_count = 0;
    button_at(8, 0x10, 150, 75);
    button_at(8, 0x10, 150, 75);
    check(middle_count == 4 && middle_message[0] == 0xa4 && middle_message[1] == 0xa5 &&
          middle_message[2] == 0xa6 && middle_message[3] == 0xa5, 501);
    middle_count = 0;
    button_at(8, 0x10, 500, 375);
    button_at(8, 0x10, 500, 375);
    check(middle_count == 4 && middle_message[0] == 0x204 && middle_message[2] == 0x206, 502);
    middle_count = 0;
    click_queued(2, 4, 150, 75);
    click_queued(2, 4, 150, 75);
    /* The move loop takes each release itself, so the record holds the two
       presses only: 00A1h, then 00A3h. */
    check(middle_count >= 2 && middle_message[0] == 0xa1 && middle_message[1] == 0xa3, 503);
    check(IsZoomed(framed), 504);
    middle_count = 0;
    click_queued(2, 4, 150, 10);
    click_queued(2, 4, 150, 10);
    check(!IsZoomed(framed), 505);
    DestroyWindow(clicks);
    DestroyWindow(framed);
}
/* a move over the caption is WM_NCMOUSEMOVE with HTCAPTION and the
   screen point, a move into the client area WM_MOUSEMOVE with client
   coordinates. */
static void nonclient_move(void)
{
    const U framed = CreateWindowExA(0, "MiddlePlain", "Move", 0x10cf0000u, 50, 65, 300, 200, 0, 0, 0, 0);
    check(framed != 0, 600);
    move_message = 0;
    mouse_event(0x8001, 150 * 65536 / 800, 75 * 65536 / 600, 0, 0);
    pump();
    check(move_message == 0xa0 && move_wp == 2 && near(move_lp, 150, 75), 601);
    move_message = 0;
    mouse_event(0x8001, 150 * 65536 / 800, 150 * 65536 / 600, 0, 0);
    pump();
    check(move_message == 0x200 && (move_lp & 0xffff) < 300 && (move_lp >> 16) < 200, 602);
    DestroyWindow(framed);
}
void mainCRTStartup(void)
{
    font_smoothing();
    swap_buttons();
    sized_bitmap();
    middle_button();
    frame_double_click();
    nonclient_move();
    ExitProcess(0);
}
