/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A window that blits a new picture to the DirectDraw primary surface about
   sixty times a second, through a clipper, the way a windowed game does.
   Its colours hold no white and no black, so every white and black pixel in
   its client area is the pointer. tools/test_ddraw_pointer_host.py. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcA(h, m, w, l);
}
static void require(int okay, UINT code) { if (!okay) ExitProcess(code); }
void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    c.lpfnWndProc = proc; c.lpszClassName = "DDPointer"; c.hCursor = LoadCursorA(0, IDC_ARROW);
    RegisterClassA(&c);
    HWND h = CreateWindowA(c.lpszClassName, "DDPointer", WS_OVERLAPPEDWINDOW, 100, 80, 420, 320, 0, 0, 0, 0);
    require(h != 0, 1);
    ShowWindow(h, SW_SHOW); UpdateWindow(h); SetForegroundWindow(h);
    LPDIRECTDRAW dd = 0; LPDIRECTDRAWSURFACE primary = 0, back = 0; LPDIRECTDRAWCLIPPER clip = 0;
    require(DirectDrawCreate(0, &dd, 0) == 0, 2);
    require(IDirectDraw_SetCooperativeLevel(dd, h, DDSCL_NORMAL) == 0, 3);
    DDSURFACEDESC d = {0};
    d.dwSize = sizeof d; d.dwFlags = DDSD_CAPS; d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    require(IDirectDraw_CreateSurface(dd, &d, &primary, 0) == 0, 4);
    require(IDirectDraw_CreateClipper(dd, 0, &clip, 0) == 0, 5);
    require(IDirectDrawClipper_SetHWnd(clip, 0, h) == 0, 6);
    require(IDirectDrawSurface_SetClipper(primary, clip) == 0, 7);
    RECT client; GetClientRect(h, &client);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d.dwWidth = client.right; d.dwHeight = client.bottom;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    require(IDirectDraw_CreateSurface(dd, &d, &back, 0) == 0, 8);
    OutputDebugStringA("DDPOINTER running");
    for (unsigned frame = 0;; ++frame) {
        MSG m;
        while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) ExitProcess(0);
            TranslateMessage(&m); DispatchMessageA(&m);
        }
        DDBLTFX fx = {0};
        fx.dwSize = sizeof fx; fx.dwFillColor = frame & 1 ? 0x00208040 : 0x00406080;
        require(IDirectDrawSurface_Blt(back, 0, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &fx) == 0, 9);
        POINT p = {0, 0}; ClientToScreen(h, &p);
        RECT r = {p.x, p.y, p.x + client.right, p.y + client.bottom};
        require(IDirectDrawSurface_Blt(primary, &r, back, 0, DDBLT_WAIT, 0) == 0, 10);
        Sleep(16);
    }
}
