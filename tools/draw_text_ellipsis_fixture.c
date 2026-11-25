/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DrawTextEx with DT_END_ELLIPSIS and without DT_NOPREFIX, the way COMCTL32
   draws a tab label with "&": a label that fits keeps its prefix and draws
   as without the ellipsis; one that does not fit is shortened as plain text.
   Writes C:\DTEXT.TXT, eight ints: the answers of the four calls, whether the
   wide label drew as without the ellipsis, the dark pixels and the rightmost
   one of the narrow label, and whether it drew as the plain text with
   DT_NOPREFIX. tools/test_draw_text_ellipsis_host.py */
#include <windows.h>

enum { Width = 200, Height = 20 };
static COLORREF pictures[2][Width * Height];

static void clear(HDC dc)
{
    RECT all = {0, 0, Width, Height};
    FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
}

static void grab(HDC dc, COLORREF *out)
{
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            out[y * Width + x] = GetPixel(dc, x, y);
}

static int same(void)
{
    for (int i = 0; i < Width * Height; ++i)
        if (pictures[0][i] != pictures[1][i]) return 0;
    return 1;
}

void mainCRTStartup(void)
{
    int results[8] = {0};
    HDC screen = GetDC(NULL), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, Width, Height);
    SelectObject(dc, bitmap);
    SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    RECT wide = {0, 0, Width, Height}, narrow = {0, 0, 40, Height};
    char label[] = "&Date && Time", plain[] = "Date & Time";

    clear(dc);
    results[0] = DrawTextExA(dc, label, -1, &wide, DT_SINGLELINE | DT_END_ELLIPSIS, NULL);
    grab(dc, pictures[0]);
    clear(dc);
    results[1] = DrawTextExA(dc, label, -1, &wide, DT_SINGLELINE, NULL);
    grab(dc, pictures[1]);
    results[4] = same();

    clear(dc);
    results[2] = DrawTextExA(dc, label + 1, -1, &narrow, DT_SINGLELINE | DT_END_ELLIPSIS, NULL);
    grab(dc, pictures[0]);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            if (pictures[0][y * Width + x] != RGB(255, 255, 255))
            {
                ++results[5];
                if (x > results[6]) results[6] = x;
            }
    clear(dc);
    results[3] = DrawTextExA(dc, plain, -1, &narrow, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX, NULL);
    grab(dc, pictures[1]);
    results[7] = same();

    HANDLE file = CreateFileA("C:\\DTEXT.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;
    WriteFile(file, results, sizeof results, &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
