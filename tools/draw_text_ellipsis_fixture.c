/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DrawTextEx with DT_END_ELLIPSIS and without DT_NOPREFIX, the way COMCTL32
   draws a tab label with "&": a label that fits keeps its prefix and draws
   as without the ellipsis; one that does not fit is shortened as plain text.
   DT_WORD_ELLIPSIS is the end ellipsis per line; a list view label measured
   with it (42C11h) wraps where it is too wide and keeps a first word that
   is too wide whole.
   Writes C:\DTEXT.TXT, fourteen ints: the answers of the four calls, whether
   the wide label drew as without the ellipsis, the dark pixels and the
   rightmost one of the narrow label, and whether it drew as the plain text
   with DT_NOPREFIX; then the height of a line, the heights DT_CALCRECT
   answers with the word ellipsis for "Dial-Up Networking" and for one word
   too wide, the answer of a drawn word ellipsis on several lines and its
   dark pixels and rightmost one. tools/test_draw_text_ellipsis_host.py */
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
    int results[14] = {0};
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

    /* The word ellipsis: how the list view measures a label, and drawn. */
    RECT line = {0, 0, Width, 0}, label71 = {0, 0, 71, 0}, word = {0, 0, 30, 0}, box = {0, 0, 40, Height};
    results[8] = DrawTextA(dc, "Dial-Up", -1, &line, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    results[9] = DrawTextA(dc, "Dial-Up Networking", -1, &label71, 0x42c11);
    results[10] = DrawTextA(dc, "VeryLongUnbrokenWord", -1, &word, 0x42c11);
    clear(dc);
    results[11] = DrawTextA(dc, "Alpha VeryLongUnbrokenWord", -1, &box,
                            DT_WORDBREAK | DT_NOPREFIX | DT_WORD_ELLIPSIS);
    grab(dc, pictures[0]);
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            if (pictures[0][y * Width + x] != RGB(255, 255, 255))
            {
                ++results[12];
                if (x > results[13]) results[13] = x;
            }

    HANDLE file = CreateFileA("C:\\DTEXT.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD written = 0;
    WriteFile(file, results, sizeof results, &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
