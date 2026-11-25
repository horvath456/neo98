/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The frame painting MFC's control bars rely on (tools/test_frame_paint_host.py).

   Red: a child with a 6-pixel frame from WM_NCCALCSIZE that paints the frame
   the way CControlBar::EraseNonClient does: a window DC without the client,
   and WM_ERASEBKGND with that DC, which DefWindowProc fills with the red
   class brush. The window is shown after it was created, so the frame must
   outlive the show.
   Green: ExtTextOut with ETO_OPAQUE and a negative width, as
   CDC::FillSolidRect draws the right line of Draw3dRect.
   Yellow: a dock with a 4-pixel frame at the top, painted through its window
   DC, and a cyan bar that reaches 4 pixels up into that frame, as CDockBar
   places its toolbars. The bar is cut to the dock's client, so the whole
   frame of the dock is yellow. */
#include <windows.h>

static LRESULT CALLBACK bordered(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_NCCALCSIZE: { RECT *r = (RECT *)l; InflateRect(r, -6, -6); return 0; }
    case WM_NCPAINT:
    {
        HDC dc = GetWindowDC(h);
        RECT client, window;
        GetClientRect(h, &client); GetWindowRect(h, &window);
        ScreenToClient(h, (POINT *)&window.left); ScreenToClient(h, (POINT *)&window.right);
        OffsetRect(&client, -window.left, -window.top);
        ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
        OffsetRect(&window, -window.left, -window.top);
        IntersectClipRect(dc, window.left, window.top, window.right, window.bottom);
        SendMessage(h, WM_ERASEBKGND, (WPARAM)dc, 0);
        ReleaseDC(h, dc);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
        FillRect(dc, &ps.rcPaint, blue); DeleteObject(blue);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProc(h, m, w, l);
}

static LRESULT CALLBACK dock(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_NCCALCSIZE: { RECT *r = (RECT *)l; r->top += 4; return 0; }
    case WM_NCPAINT:
    {
        HDC dc = GetWindowDC(h);
        RECT window; GetWindowRect(h, &window); OffsetRect(&window, -window.left, -window.top);
        ExcludeClipRect(dc, 0, 4, window.right, window.bottom);
        HBRUSH yellow = CreateSolidBrush(RGB(255, 255, 0));
        FillRect(dc, &window, yellow); DeleteObject(yellow);
        ReleaseDC(h, dc);
        return 0;
    }
    }
    return DefWindowProc(h, m, w, l);
}

static LRESULT CALLBACK bar(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        HBRUSH cyan = CreateSolidBrush(RGB(0, 255, 255));
        FillRect(dc, &ps.rcPaint, cyan); DeleteObject(cyan);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static LRESULT CALLBACK frame(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT r = {260, 20, 230, 40};
        SetBkColor(dc, RGB(0, 255, 0));
        ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &r, 0, 0, 0);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR line, int show)
{
    WNDCLASSA k = {0};
    k.hInstance = instance; k.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    k.lpfnWndProc = frame; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); k.lpszClassName = "FramePaint";
    RegisterClassA(&k);
    k.lpfnWndProc = bordered; k.hbrBackground = CreateSolidBrush(RGB(255, 0, 0)); k.lpszClassName = "FrameBordered";
    RegisterClassA(&k);
    k.lpfnWndProc = dock; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); k.lpszClassName = "FrameDock";
    RegisterClassA(&k);
    k.lpfnWndProc = bar; k.lpszClassName = "FrameBar";
    RegisterClassA(&k);
    HWND top = CreateWindowA("FramePaint", "Frame paint", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             100, 100, 400, 300, 0, 0, instance, 0);
    HWND child = CreateWindowA("FrameBordered", "", WS_CHILD | WS_CLIPSIBLINGS, 20, 20, 200, 60, top, 0, instance, 0);
    HWND docked = CreateWindowA("FrameDock", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                20, 120, 300, 60, top, 0, instance, 0);
    CreateWindowA("FrameBar", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, -4, 100, 30, docked, 0, instance, 0);
    ShowWindow(child, SW_SHOWNA);
    ShowWindow(top, SW_SHOW);
    UpdateWindow(top);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) { TranslateMessage(&message); DispatchMessageA(&message); }
    return 0;
}

void mainCRTStartup(void) { ExitProcess(WinMain(GetModuleHandleA(0), 0, 0, SW_SHOW)); }
