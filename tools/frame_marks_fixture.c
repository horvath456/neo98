/* SPDX-License-Identifier: GPL-2.0-or-later
 * Check boxes, radio buttons and a menu bar with right-justified items.
 * The frame carries three BUTTON controls: a checked
 * BS_AUTOCHECKBOX, a checked and an unchecked BS_AUTORADIOBUTTON. Its menu
 * bar has "Xy", "\bXy" (a leading 08h, the old right-alignment mark) and
 * "Zz" with MF_HELP. After the first paint the guest writes the screen
 * rectangles of the three menu items into its title, as decimal numbers, so
 * the test can find them in the screenshot. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(const void *);
API U CreateMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U SetMenu(U, U);
API U GetMenuItemRect(U, U, U, int *);
API U SetWindowTextA(U, const char *);
API U SendMessageA(U, U, U, U);
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static char *number(char *out, int value)
{
    char digits[12];
    int n = 0;
    if (value < 0) { *out++ = '-'; value = -value; }
    do digits[n++] = (char)('0' + value % 10); while ((value /= 10) && n < 11);
    while (n) *out++ = digits[--n];
    *out++ = ' ';
    return out;
}
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 16, 0, (U)"Neo98FrameMarks"};
    if (!RegisterClassA(klass)) ExitProcess(16701);
    U menu = CreateMenu();
    if (!menu) ExitProcess(16702);
    if (!AppendMenuA(menu, 0, 1, "Xy") || !AppendMenuA(menu, 0, 2, "\bXy") ||
        !AppendMenuA(menu, 0x4000, 3, "Zz")) ExitProcess(16703);
    U frame = CreateWindowExA(0, "Neo98FrameMarks", "marks", 0x10cf0000, 20, 20, 420, 260, 0, menu, instance, 0);
    if (!frame) ExitProcess(16704);
    /* BS_AUTOCHECKBOX, BS_AUTORADIOBUTTON twice; BM_SETCHECK on the first two. */
    U check = CreateWindowExA(0, "BUTTON", "Check", 0x50010003, 20, 20, 120, 20, frame, 101, instance, 0);
    U radio = CreateWindowExA(0, "BUTTON", "Radio on", 0x50030009, 20, 50, 120, 20, frame, 102, instance, 0);
    U plain = CreateWindowExA(0, "BUTTON", "Radio off", 0x50000009, 20, 80, 120, 20, frame, 103, instance, 0);
    if (!check || !radio || !plain) ExitProcess(16705);
    SendMessageA(check, 0xf1, 1, 0);
    SendMessageA(radio, 0xf1, 1, 0);
    ShowWindow(frame, 5);
    UpdateWindow(frame);
    char title[128], *at = title;
    for (U i = 0; i < 3; ++i)
    {
        int r[4];
        if (!GetMenuItemRect(frame, menu, i, r)) ExitProcess(16706 + i);
        for (U k = 0; k < 4; ++k) at = number(at, r[k]);
    }
    *at = 0;
    SetWindowTextA(frame, title);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
    ExitProcess(0);
}
