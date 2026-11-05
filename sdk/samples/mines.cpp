/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Minesweeper as a Win32 program for ARM64. The rules are the reconstructed
// game logic of this port, src/winmine.cpp (; the window,
// the menu and every pixel are drawn here with USER32 and GDI32. No bitmap
// and no code of the original program is part of it.
//
// python3 neo98.py sdk-build sdk/samples/mines.cpp src/winmine.cpp sdk/samples/mines.rc \
// -o mines.exe --gui -DWINMINE_NO_SELF_TEST -Isrc
#include <windows.h>
#include <windowsx.h>
#include "winmine.h"
#include "mines.h"

namespace
{
const int Cell = 16, Left = 12, Top = 55, Border = 3;
Winmine::Game game;
HINSTANCE instance;
HWND window;
bool capture, face_down;
int level = IDM_BEGINNER;

int BoardWidth() { return game.columns * Cell; }
int BoardHeight() { return game.rows * Cell; }
int ClientWidth() { return Left * 2 + BoardWidth(); }
int ClientHeight() { return Top + BoardHeight() + Left; }
RECT FaceRect()
{
    RECT r;
    SetRect(&r, ClientWidth() / 2 - 12, 16, ClientWidth() / 2 + 12, 40);
    return r;
}

void Frame(HDC dc, int left, int top, int right, int bottom, bool sunken, int width)
{
    RECT r;
    for (int i = 0; i < width; ++i)
    {
        SetRect(&r, left + i, top + i, right - i, bottom - i);
        HPEN light = CreatePen(PS_SOLID, 1, sunken ? RGB(128, 128, 128) : RGB(255, 255, 255));
        HPEN dark = CreatePen(PS_SOLID, 1, sunken ? RGB(255, 255, 255) : RGB(128, 128, 128));
        HGDIOBJ old = SelectObject(dc, light);
        MoveToEx(dc, r.left, r.bottom - 1, NULL);
        LineTo(dc, r.left, r.top);
        LineTo(dc, r.right - 1, r.top);
        SelectObject(dc, dark);
        LineTo(dc, r.right - 1, r.bottom - 1);
        LineTo(dc, r.left, r.bottom - 1);
        SelectObject(dc, old);
        DeleteObject(light);
        DeleteObject(dark);
    }
}

void Fill(HDC dc, int left, int top, int right, int bottom, COLORREF color)
{
    RECT r;
    SetRect(&r, left, top, right, bottom);
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void Mine(HDC dc, int x, int y)
{
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    HGDIOBJ old = SelectObject(dc, black);
    Ellipse(dc, x + 4, y + 4, x + 13, y + 13);
    SelectObject(dc, old);
    MoveToEx(dc, x + 8, y + 2, NULL); LineTo(dc, x + 8, y + 15);
    MoveToEx(dc, x + 2, y + 8, NULL); LineTo(dc, x + 15, y + 8);
    SetPixel(dc, x + 6, y + 6, RGB(255, 255, 255));
}

// One cell by its state: 0 open, 1..8 a number, 9 a pressed question mark,
// 10 a mine, 11 a wrong flag, 12 the mine that exploded, 13 a question mark,
// 14 a flag, 15 closed.
void DrawCell(HDC dc, int column, int row)
{
    static const COLORREF colours[9] = {0, RGB(0, 0, 255), RGB(0, 128, 0), RGB(255, 0, 0), RGB(0, 0, 128),
                                        RGB(128, 0, 0), RGB(0, 128, 128), RGB(0, 0, 0), RGB(128, 128, 128)};
    const int state = game.Cell(row, column) & 0x1f;
    const int x = Left + (column - 1) * Cell, y = Top + (row - 1) * Cell;
    const bool raised = state >= 13;
    Fill(dc, x, y, x + Cell, y + Cell, state == 12 ? RGB(255, 0, 0) : RGB(192, 192, 192));
    if (raised) Frame(dc, x, y, x + Cell, y + Cell, false, 2);
    else
    {
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
        HGDIOBJ old = SelectObject(dc, grid);
        MoveToEx(dc, x, y + Cell - 1, NULL);
        LineTo(dc, x, y);
        LineTo(dc, x + Cell, y);
        SelectObject(dc, old);
        DeleteObject(grid);
    }
    char text[2] = {0, 0};
    COLORREF colour = 0;
    if (state >= 1 && state <= 8) { text[0] = (char)('0' + state); colour = colours[state]; }
    else if (state == 9 || state == 13) text[0] = '?';
    if (text[0])
    {
        RECT r;
        SetRect(&r, x + 1, y + 1, x + Cell, y + Cell);
        SetTextColor(dc, colour);
        DrawTextA(dc, text, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (state == 10 || state == 12) Mine(dc, x, y);
    if (state == 11)
    {
        Mine(dc, x, y);
        HPEN red = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
        HGDIOBJ old = SelectObject(dc, red);
        MoveToEx(dc, x + 2, y + 2, NULL); LineTo(dc, x + 14, y + 14);
        MoveToEx(dc, x + 13, y + 2, NULL); LineTo(dc, x + 1, y + 14);
        SelectObject(dc, old);
        DeleteObject(red);
    }
    if (state == 14)
    {
        Fill(dc, x + 5, y + 3, x + 9, y + 8, RGB(255, 0, 0));
        Fill(dc, x + 8, y + 3, x + 9, y + 12, RGB(0, 0, 0));
        Fill(dc, x + 5, y + 11, x + 12, y + 13, RGB(0, 0, 0));
    }
}

void Counter(HDC dc, int right, int value)
{
    char text[8];
    RECT r;
    if (value < -99) value = -99;
    if (value > 999) value = 999;
    wsprintfA(text, value < 0 ? "-%02d" : "%03d", value < 0 ? -value : value);
    SetRect(&r, right - 41, 16, right, 40);
    Fill(dc, r.left, r.top, r.right, r.bottom, RGB(0, 0, 0));
    Frame(dc, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1, true, 1);
    SetTextColor(dc, RGB(255, 0, 0));
    DrawTextA(dc, text, 3, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void Face(HDC dc)
{
    const RECT r = FaceRect();
    const int kind = face_down ? 4 : capture ? 1 : game.face;
    Fill(dc, r.left, r.top, r.right, r.bottom, RGB(192, 192, 192));
    Frame(dc, r.left, r.top, r.right, r.bottom, face_down, 2);
    const int x = r.left + 4 + (face_down ? 1 : 0), y = r.top + 4 + (face_down ? 1 : 0);
    HBRUSH yellow = CreateSolidBrush(RGB(255, 255, 0));
    HGDIOBJ old = SelectObject(dc, yellow);
    Ellipse(dc, x, y, x + 16, y + 16);
    SelectObject(dc, old);
    DeleteObject(yellow);
    // Eyes and mouth: smiling, surprised, dead, won.
    if (kind == 2)
    {
        MoveToEx(dc, x + 4, y + 4, NULL); LineTo(dc, x + 7, y + 7);
        MoveToEx(dc, x + 6, y + 4, NULL); LineTo(dc, x + 3, y + 7);
        MoveToEx(dc, x + 9, y + 4, NULL); LineTo(dc, x + 12, y + 7);
        MoveToEx(dc, x + 11, y + 4, NULL); LineTo(dc, x + 8, y + 7);
        MoveToEx(dc, x + 5, y + 12, NULL); LineTo(dc, x + 8, y + 10); LineTo(dc, x + 11, y + 12);
    }
    else
    {
        if (kind == 3) Fill(dc, x + 3, y + 5, x + 13, y + 8, RGB(0, 0, 0));
        else { Fill(dc, x + 5, y + 5, x + 7, y + 7, RGB(0, 0, 0)); Fill(dc, x + 9, y + 5, x + 11, y + 7, RGB(0, 0, 0)); }
        if (kind == 1) { HGDIOBJ none = SelectObject(dc, GetStockObject(NULL_BRUSH)); Ellipse(dc, x + 6, y + 9, x + 10, y + 13); SelectObject(dc, none); }
        else { MoveToEx(dc, x + 5, y + 10, NULL); LineTo(dc, x + 8, y + 12); LineTo(dc, x + 11, y + 9); }
    }
}

void Paint(HDC dc)
{
    Fill(dc, 0, 0, ClientWidth(), ClientHeight(), RGB(192, 192, 192));
    Frame(dc, 0, 0, ClientWidth(), ClientHeight(), false, Border);
    Frame(dc, 9, 9, ClientWidth() - 9, 46, true, 2);
    Frame(dc, Left - Border, Top - Border, Left + BoardWidth() + Border, Top + BoardHeight() + Border, true, Border);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontA(-13, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "MS Sans Serif");
    HGDIOBJ old = SelectObject(dc, font);
    Counter(dc, 16 + 41, game.remaining);
    Counter(dc, ClientWidth() - 16, game.seconds);
    Face(dc);
    for (int row = 1; row <= game.rows; ++row)
        for (int column = 1; column <= game.columns; ++column)
            DrawCell(dc, column, row);
    SelectObject(dc, old);
    DeleteObject(font);
}

void Title()
{
    // The state of the game in the caption as well, for a reader without eyes.
    char text[96];
    static const char *const faces[] = {"playing", "playing", "lost", "won", "playing"};
    wsprintfA(text, "Minesweeper - %s, %d left, %d open", faces[game.face & 3], game.remaining, game.revealed);
    SetWindowTextA(window, text);
}

void NewGame(int width, int height, int count)
{
    game.Configure(width, height, count, GetTickCount());
    game.Reset();
    RECT r;
    SetRect(&r, 0, 0, ClientWidth(), ClientHeight());
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE);
    SetWindowPos(window, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    InvalidateRect(window, NULL, FALSE);
    Title();
}

void Level(int id)
{
    HMENU menu = GetMenu(window);
    CheckMenuItem(menu, level, MF_BYCOMMAND | MF_UNCHECKED);
    level = id;
    CheckMenuItem(menu, level, MF_BYCOMMAND | MF_CHECKED);
    if (id == IDM_BEGINNER) NewGame(8, 8, 10);
    else if (id == IDM_INTERMEDIATE) NewGame(16, 16, 40);
    else NewGame(30, 16, 99);
}

void CellAt(LPARAM lParam, int &row, int &column)
{
    const int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
    row = column = -2;
    if (x >= Left && y >= Top && x < Left + BoardWidth() && y < Top + BoardHeight())
    {
        column = (x - Left) / Cell + 1;
        row = (y - Top) / Cell + 1;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int row, column;
    POINT point;
    switch (message)
    {
    case WM_CREATE:
        window = hwnd;
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    case WM_TIMER:
    {
        const int before = game.seconds;
        game.Tick();
        if (game.seconds != before) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
        point.x = GET_X_LPARAM(lParam);
        point.y = GET_Y_LPARAM(lParam);
        const RECT face = FaceRect();
        CellAt(lParam, row, column);
        if (message == WM_LBUTTONDOWN && PtInRect(&face, point)) face_down = true;
        else if (game.Active() && game.Inside(row, column))
        {
            game.Preview(-2, -2);
            game.chord = message == WM_MBUTTONDOWN || (wParam & MK_RBUTTON) != 0;
            capture = true;
            game.Preview(row, column);
        }
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_RBUTTONDOWN:
        CellAt(lParam, row, column);
        if (!game.Active()) return 0;
        if (capture) { game.Preview(-2, -2); game.chord = 1; game.Preview(row, column); }
        else game.Mark(row, column);
        InvalidateRect(hwnd, NULL, FALSE);
        Title();
        return 0;
    case WM_MOUSEMOVE:
        if (!capture) return 0;
        CellAt(lParam, row, column);
        game.Preview(row, column);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        if (capture)
        {
            CellAt(lParam, row, column);
            game.Preview(row, column);
            if (game.Active()) game.Release();
            game.Preview(-2, -2);
            game.chord = 0;
            capture = false;
        }
        if (face_down && message == WM_LBUTTONUP)
        {
            const RECT face = FaceRect();
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
            face_down = false;
            if (PtInRect(&face, point)) Level(level);
        }
        if (!(wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON))) ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
        Title();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_F2) Level(level);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_NEW: Level(level); return 0;
        case IDM_BEGINNER:
        case IDM_INTERMEDIATE:
        case IDM_EXPERT: Level(LOWORD(wParam)); return 0;
        case IDM_MARKS:
            game.questions ^= 1;
            CheckMenuItem(GetMenu(hwnd), IDM_MARKS, MF_BYCOMMAND | (game.questions ? MF_CHECKED : MF_UNCHECKED));
            return 0;
        case IDM_ABOUT:
            MessageBoxA(hwnd, "Minesweeper for ARM64.\nThe rules are the reconstructed game logic of NEO98.",
                        "About Minesweeper", MB_OK | MB_ICONINFORMATION);
            return 0;
        case IDM_EXIT: DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        Paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR command, int show)
{
    WNDCLASSA wc;
    MSG message;
    instance = hInstance;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MINES);
    wc.lpszClassName = "Arm64Mines";
    if (!RegisterClassA(&wc)) return 1;
    // A number on the command line is the seed, so a test plays a known board.
    unsigned seed = 0;
    for (const char *p = command; *p >= '0' && *p <= '9'; ++p) seed = seed * 10 + (unsigned)(*p - '0');
    game.Configure(8, 8, 10, seed ? seed : GetTickCount());
    game.Reset();
    game.questions = 1;
    RECT r;
    SetRect(&r, 0, 0, ClientWidth(), ClientHeight());
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE);
    HWND hwnd = CreateWindowA("Arm64Mines", "Minesweeper", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              80, 80, r.right - r.left, r.bottom - r.top, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 2;
    CheckMenuItem(GetMenu(hwnd), IDM_BEGINNER, MF_BYCOMMAND | MF_CHECKED);
    CheckMenuItem(GetMenu(hwnd), IDM_MARKS, MF_BYCOMMAND | MF_CHECKED);
    Title();
    ShowWindow(hwnd, show == SW_SHOWDEFAULT ? SW_SHOWNORMAL : show);
    UpdateWindow(hwnd);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return (int)message.wParam;
}
