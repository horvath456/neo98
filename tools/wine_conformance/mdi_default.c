/* SPDX-License-Identifier: GPL-2.0-or-later
 * CW_USEDEFAULT MDI children in small clients: raw cascade size or the
 * WM_GETMINMAXINFO minimum tracking size? */
#include <windows.h>
#include "wine/test.h"

static void children(int width, int height, int count)
{
    CLIENTCREATESTRUCT ccs = {0, 1000};
    HWND frame, client, child = 0;
    RECT r;
    int i;
    frame = CreateWindowExA(0, "MdiDefaultFrame", "frame", WS_OVERLAPPEDWINDOW, 0, 0, 800, 1500, 0, 0, GetModuleHandleA(NULL), 0);
    client = CreateWindowExA(0, "MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE, 0, 0, width, height,
                             frame, 0, GetModuleHandleA(NULL), &ccs);
    ok(frame && client, "%dx%d: frame and client\n", width, height);
    GetClientRect(client, &r);
    trace("%dx%d: client %ld,%ld\n", width, height, r.right, r.bottom);
    for (i = 0; i < count; ++i)
    {
        MDICREATESTRUCTA mcs = {"MdiDefaultChild", "child", GetModuleHandleA(NULL),
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, WS_OVERLAPPEDWINDOW, 0};
        child = (HWND)SendMessageA(client, WM_MDICREATE, 0, (LPARAM)&mcs);
        ok(child != NULL, "%dx%d: child %d\n", width, height, i);
        if (!child) break;
        GetWindowRect(child, &r);
        MapWindowPoints(0, client, (POINT *)&r, 2);
        trace("%dx%d child %d: %ld,%ld %ldx%ld\n", width, height, i, r.left, r.top, r.right - r.left, r.bottom - r.top);
        if (width == 50)
            ok(r.right - r.left == GetSystemMetrics(SM_CXMINTRACK) && r.bottom - r.top == GetSystemMetrics(SM_CYMINTRACK),
               "%dx%d child %d: minimum tracking size %ldx%ld\n", width, height, i, r.right - r.left, r.bottom - r.top);
        if (width == 716 && i == 0)
            ok(r.bottom - r.top == min(940, GetSystemMetrics(SM_CYMAXTRACK)),
               "%dx%d: height %ld, raw cascade 940, maximum tracking %d\n", width, height,
               r.bottom - r.top, GetSystemMetrics(SM_CYMAXTRACK));
        if (width == 300 && i == 0)
            ok(r.right - r.left >= GetSystemMetrics(SM_CXMINTRACK), "%dx%d: width %ld at least the minimum\n",
               width, height, r.right - r.left);
    }
    DestroyWindow(frame);
}

START_TEST(mdi_default)
{
    WNDCLASSA cls = {0};
    cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(NULL);
    cls.hCursor = LoadCursorA(0, IDC_ARROW); cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = "MdiDefaultFrame"; ok(RegisterClassA(&cls) != 0, "frame class\n");
    cls.lpfnWndProc = (WNDPROC)DefMDIChildProcA; cls.lpszClassName = "MdiDefaultChild";
    ok(RegisterClassA(&cls) != 0, "child class\n");
    children(640, 400, 3);
    children(300, 700, 3);
    children(200, 60, 3);
    children(50, 10, 3);
    children(716, 1380, 2);
}
