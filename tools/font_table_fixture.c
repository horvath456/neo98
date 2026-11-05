/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a process keeps many logical fonts alive at once. Win98 keeps them
   in the GDI heap without a count of its own. RetroZilla probes its font list
   and keeps every font; the port's table held 64, every further
   CreateFontIndirect failed with error 8, and Mozilla's dialogs fell back to
   the System font. 300 fonts stay alive here; the last one must still be the
   MS Sans Serif it asked for, 13 pixels high and not bold. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFontIndirectA(const void *);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U GetTextMetricsA(U, void *);
API U GetTextFaceA(U, int, char *);
API U DeleteObject(U);
API U DeleteDC(U);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }

void mainCRTStartup(void)
{
    static U fonts[300];
    unsigned char logfont[60];
    for (int i = 0; i < 300; ++i)
    {
        for (int b = 0; b < 60; ++b) logfont[b] = 0;
        /* lfHeight -11.. -40, alternating weights, MS Sans Serif. */
        *(int *)logfont = -(11 + i % 30);
        *(int *)(logfont + 16) = i & 1 ? 700 : 400;
        logfont[23] = 1; /* DEFAULT_CHARSET, as Mozilla asks */
        const char *face = "MS Sans Serif";
        for (int c = 0; face[c]; ++c) logfont[28 + c] = (unsigned char)face[c];
        fonts[i] = CreateFontIndirectA(logfont);
        check(fonts[i] != 0, 10 + (i < 64 ? 0 : 1));
    }
    for (int b = 0; b < 60; ++b) logfont[b] = 0;
    *(int *)logfont = -11;
    *(int *)(logfont + 16) = 400;
    logfont[23] = 1;
    const char *face = "MS Sans Serif";
    for (int c = 0; face[c]; ++c) logfont[28 + c] = (unsigned char)face[c];
    U last = CreateFontIndirectA(logfont), dc = CreateCompatibleDC(0);
    check(last != 0 && dc != 0, 20);
    check(SelectObject(dc, last) != 0, 21);
    int metrics[16] = {0};
    char name[32] = {0};
    check(GetTextMetricsA(dc, metrics) != 0, 22);
    check(metrics[0] == 13, 23);          /* tmHeight: MS Sans Serif 8 */
    check(metrics[7] == 400, 24);         /* tmWeight */
    check(GetTextFaceA(dc, 32, name) != 0 && name[0] == 'M' && name[3] == 'S' && name[4] == 'a', 25);
    DeleteDC(dc);
    DeleteObject(last);
    for (int i = 0; i < 300; ++i) check(DeleteObject(fonts[i]) != 0, 30);
    ExitProcess(0);
}
