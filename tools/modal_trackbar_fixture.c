// SPDX-License-Identifier: GPL-2.0-or-later
// A repeating original COMCTL32 trackbar opens a modal warning from WM_HSCROLL.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(void *);
API U MessageBoxA(U, const char *, const char *, U);
API U SendMessageA(U, U, U, U);
API void InitCommonControls(void);
static U __stdcall procedure(U window, U message, U wp, U lp)
{
    if (message == 0x114 && (wp & 0xffff) != 4 && (wp & 0xffff) != 8)
        MessageBoxA(window, "A setting could not be changed.", "Trackbar warning", 0x30);
    return DefWindowProcA(window, message, wp, lp);
}
void mainCRTStartup(void)
{
    InitCommonControls();
    U instance = GetModuleHandleA(0);
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 16, 0, (U)"ModalTrackbar98"};
    RegisterClassA(klass);
    U window = CreateWindowExA(0, "ModalTrackbar98", "Modal trackbar", 0x90cf0000,
                              64, 55, 300, 180, 0, 0, instance, 0);
    U slider = CreateWindowExA(0, "msctls_trackbar32", "slider", 0x50010001,
                              20, 30, 200, 30, window, 100, instance, 0);
    SendMessageA(slider, 0x406, 1, 100u << 16);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
}
