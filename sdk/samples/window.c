/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A window program: a menu and a dialog from resources, drawing in WM_PAINT,
   a timer that counts, the keyboard and the mouse. */
#include <windows.h>
#include <windowsx.h>
#include "window.h"

static HINSTANCE instance;
static int ticks, clicks, ellipse;
static POINT last_click;
static char name[64] = "world";

static INT_PTR CALLBACK AboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    char format[64], text[160];
    (void)lParam;
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemTextA(dialog, IDC_NAME, name);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NAME && HIWORD(wParam) == EN_CHANGE)
        {
            GetDlgItemTextA(dialog, IDC_NAME, name, sizeof name);
            LoadStringA(instance, IDS_GREETING, format, sizeof format);
            wsprintfA(text, format, name);
            SetDlgItemTextA(dialog, IDC_GREETING, text);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void Paint(HWND hwnd)
{
    PAINTSTRUCT paint;
    RECT client, shape;
    char text[160];
    HDC dc = BeginPaint(hwnd, &paint);
    GetClientRect(hwnd, &client);
    HBRUSH brush = CreateSolidBrush(RGB(0, 128, 128));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(128, 0, 0));
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    SetRect(&shape, 20, 40, 20 + 120, 40 + 80);
    if (ellipse) Ellipse(dc, shape.left, shape.top, shape.right, shape.bottom);
    else Rectangle(dc, shape.left, shape.top, shape.right, shape.bottom);
    MoveToEx(dc, 160, 40, NULL);
    LineTo(dc, 260, 120);
    LineTo(dc, 160, 120);
    LineTo(dc, 160, 40);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
    SetBkMode(dc, TRANSPARENT);
    wsprintfA(text, "ticks %d   clicks %d   last click %d,%d   hello %s", ticks, clicks, last_click.x, last_click.y, name);
    TextOutA(dc, 20, 12, text, lstrlenA(text));
    SetRect(&shape, 20, 130, client.right - 20, 150);
    DrawEdge(dc, &shape, EDGE_SUNKEN, BF_RECT);
    DrawTextA(dc, "ARM64 code, the Win32 API of NEO98", -1, &shape, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(hwnd, &paint);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    char title[96], caption[128];
    switch (message)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 250, NULL);
        return 0;
    case WM_TIMER:
        ++ticks;
        LoadStringA(instance, IDS_TITLE, title, sizeof title);
        wsprintfA(caption, "%s - %d ticks, %d clicks, %s", title, ticks, clicks, name);
        SetWindowTextA(hwnd, caption);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        ++clicks;
        last_click.x = GET_X_LPARAM(lParam);
        last_click.y = GET_Y_LPARAM(lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F2) SendMessageA(hwnd, WM_COMMAND, IDM_FILE_RESET, 0);
        if (wParam == VK_F1) SendMessageA(hwnd, WM_COMMAND, IDM_HELP_ABOUT, 0);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_FILE_RESET:
            ticks = clicks = 0;
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case IDM_VIEW_ELLIPSE:
            ellipse = !ellipse;
            CheckMenuItem(GetMenu(hwnd), IDM_VIEW_ELLIPSE, MF_BYCOMMAND | (ellipse ? MF_CHECKED : MF_UNCHECKED));
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case IDM_HELP_ABOUT:
            DialogBoxParamA(instance, MAKEINTRESOURCE(IDD_ABOUT), hwnd, AboutProc, 0);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA wc;
    MSG message;
    char title[96];
    (void)previous; (void)command;
    instance = hInstance;
    memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MAIN);
    wc.lpszClassName = "Arm64WindowSample";
    if (!RegisterClassA(&wc)) return 1;
    LoadStringA(hInstance, IDS_TITLE, title, sizeof title);
    HWND hwnd = CreateWindowA("Arm64WindowSample", title, WS_OVERLAPPEDWINDOW, 60, 60, 420, 240, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 2;
    ShowWindow(hwnd, show == SW_SHOWDEFAULT ? SW_SHOWNORMAL : show);
    UpdateWindow(hwnd);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return (int)message.wParam;
}
