/* SPDX-License-Identifier: GPL-2.0-or-later */
/* TrueType through the public GDI32 API: text output, GetGlyphOutline in
   every format, GetOutlineTextMetrics, GetKerningPairs, GetFontData,
   GetCharABCWidths and a CreateFont width. The fixture reports what it
   measured as "KEY value..." lines through OutputDebugString, which the runner
   logs, in pieces of at most 400 characters (a continuation starts with "~");
   tools/test_truetype_native.py compares them against the font files. Hard
   failures leave with a code. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void OutputDebugStringA(const char *);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U SelectObject(U, U);
API U DeleteObject(U);
API U CreateFontA(int, int, int, int, int, U, U, U, U, U, U, U, U, const char *);
API U GetTextMetricsA(U, void *);
API U GetTextExtentPoint32A(U, const char *, int, int *);
API U TextOutA(U, int, int, const char *, int);
API U SetBkMode(U, U);
API U SetTextColor(U, U);
API U PatBlt(U, int, int, int, int, U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U GetGlyphOutlineA(U, U, U, void *, U, void *, const void *);
API U GetOutlineTextMetricsA(U, U, void *);
API U GetKerningPairsA(U, U, void *);
API U GetFontData(U, U, U, void *, U);
API U GetCharABCWidthsA(U, U, U, int *);
API U GetCharWidth32A(U, U, U, int *);
API U GetLastError(void);

static void fail(U code) { ExitProcess(code); }
static void check(int ok, U code) { if (!ok) fail(code); }

static char line[8192];
static U used;
static void put(const char *s) { while (*s && used < sizeof line - 1) line[used++] = *s++; }
static void num(int v)
{
    char t[16]; int n = 0; U u = v < 0 ? 0u - (U)v : (U)v;
    if (v < 0) put("-");
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) { char c[2] = {t[--n], 0}; put(c); }
}
static void hex(const unsigned char *p, U n)
{
    static const char digits[] = "0123456789abcdef";
    for (U i = 0; i < n && used < sizeof line - 3; ++i) { line[used++] = digits[p[i] >> 4]; line[used++] = digits[p[i] & 15]; }
}
static void flush(void)
{
    char piece[404];
    for (U at = 0, first = 1; at < used || first; first = 0) {
        U n = 0;
        if (!first) piece[n++] = '~';
        while (at < used && n < 400) piece[n++] = line[at++];
        piece[n] = 0;
        OutputDebugStringA(piece);
    }
    used = 0;
}

enum { Width = 256, Height = 96 };
static unsigned *pixels;
static unsigned char buffer[65536];
static const int identity[4] = {0x10000, 0, 0, 0x10000};

static void clear(U dc) { PatBlt(dc, 0, 0, Width, Height, 0xff0062); /* WHITENESS */ }
static int ink(int x, int y) { return (pixels[(Height - 1 - y) * Width + x] & 0xffffff) != 0xffffff; }

/* One glyph in the formats up to `last`. The large em stops at GGO_NATIVE:
   after a gray query of a glyph that needs more than 4000h bytes of
   scan-conversion memory the driver keeps that size and fails the next
   GGO_BITMAP. */
static void outline(U dc, U code, const char *tag, U last)
{
    int gm[5];
    for (U format = 0; format <= last; ++format) {
        U size = GetGlyphOutlineA(dc, code, format, gm, 0, 0, identity);
        put("GGO "); put(tag); put(" "); num((int)format); put(" "); num((int)size);
        if (size != ~0u) {
            put(" "); num(gm[0]); put(" "); num(gm[1]); put(" "); num(gm[2]); put(" "); num(gm[3]);
            put(" "); num((short)(gm[4] & 0xffff)); put(" "); num((short)(gm[4] >> 16));
            if (size && size <= sizeof buffer) {
                U got = GetGlyphOutlineA(dc, code, format, gm, sizeof buffer, buffer, identity);
                if (got != size) { put(" GOT "); num((int)got); flush(); fail(60 + format); }
                if (format != 1 || size <= 1024) { put(" "); hex(buffer, size > 1500 ? 1500 : size); }
            }
        }
        flush();
    }
}

void mainCRTStartup(void)
{
    U dc = CreateCompatibleDC(0);
    unsigned char info[40] = {0};
    info[0] = 40; *(int *)(info + 4) = Width; *(int *)(info + 8) = Height; info[12] = 1; info[14] = 32;
    U section = CreateDIBSection(dc, info, 0, (void **)&pixels, 0, 0);
    check(dc && section, 1);
    U old_bitmap = SelectObject(dc, section);
    SetBkMode(dc, 1);
    SetTextColor(dc, 0);
    unsigned char tm[56];

    /* Arial at a large em for the glyf comparison, and at 13 pixels. */
    U big = CreateFontA(-2048, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
    check(big && SelectObject(dc, big), 2);
    outline(dc, 'A', "arial2048-A", 2);
    outline(dc, 'W', "arial2048-W", 2);
    outline(dc, 'o', "arial2048-o", 2);
    /* GGO_GLYPH_INDEX with the glyph the cmap names for 'W' (the test passes
       nothing in; index 58 is Arial's W, checked there). */
    {
        int gm[5];
        U size = GetGlyphOutlineA(dc, 58, 0x82, gm, sizeof buffer, buffer, identity);
        put("GGOINDEX 58 "); num((int)size); put(" "); hex(buffer, size > 1500 ? 1500 : size); flush();
    }
    SelectObject(dc, old_bitmap); SelectObject(dc, section);
    U small = CreateFontA(-13, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
    check(small && SelectObject(dc, small), 3);
    DeleteObject(big);
    check(GetTextMetricsA(dc, tm), 4);
    put("TM"); for (int i = 0; i < 11; ++i) { put(" "); num(((int *)tm)[i]); }
    for (int i = 44; i < 53; ++i) { put(" "); num(tm[i]); } flush();
    outline(dc, 'W', "arial13-W", 6);
    outline(dc, 'g', "arial13-g", 6);
    /* GGO_BEZIER does not exist on Win98. */
    {
        int gm[5];
        put("BEZIER "); num((int)GetGlyphOutlineA(dc, 'W', 3, gm, 0, 0, identity)); flush();
        put("NOMAT "); num((int)GetGlyphOutlineA(dc, 'W', 0, gm, 0, 0, 0)); put(" "); num((int)GetLastError()); flush();
        const int twice[4] = {0x20000, 0, 0, 0x10000};
        put("MAT2 "); num((int)GetGlyphOutlineA(dc, 'W', 0, gm, 0, 0, twice));
        put(" "); num(gm[0]); put(" "); num(gm[1]); put(" "); num((short)(gm[4] & 0xffff)); flush();
    }
    /* The text "Ww" as drawn, row by row, and its extent. */
    {
        int size[2];
        clear(dc);
        check(GetTextExtentPoint32A(dc, "WwW", 3, size) && TextOutA(dc, 20, 10, "WwW", 3), 5);
        put("EXTENT "); num(size[0]); put(" "); num(size[1]); flush();
        for (int y = 0; y < 40; ++y) {
            put("ROW "); num(y); put(" ");
            for (int x = 0; x < 80; ++x) put(ink(x, y) ? "#" : ".");
            flush();
        }
    }
    {
        int abc[3 * 3], widths[3];
        check(GetCharABCWidthsA(dc, 'V', 'X', abc) && GetCharWidth32A(dc, 'V', 'X', widths), 6);
        put("ABC"); for (int i = 0; i < 9; ++i) { put(" "); num(abc[i]); }
        put(" W"); for (int i = 0; i < 3; ++i) { put(" "); num(widths[i]); } flush();
    }
    {
        U need = GetOutlineTextMetricsA(dc, 0, 0);
        put("OTMSIZE "); num((int)need); flush();
        check(need > 212 && need <= sizeof buffer, 7);
        check(GetOutlineTextMetricsA(dc, need, buffer) == 1, 8);
        put("OTM "); hex(buffer, need); flush();
    }
    {
        U total = GetKerningPairsA(dc, 0, 0);
        put("KERNTOTAL "); num((int)total); flush();
        if (total && total * 8 <= sizeof buffer) {
            U got = GetKerningPairsA(dc, total, buffer);
            put("KERN "); num((int)got); put(" "); hex(buffer, got * 8 > 2000 ? 2000 : got * 8); flush();
        }
    }
    {
        U all = GetFontData(dc, 0, 0, 0, 0);
        U head = GetFontData(dc, 0x64616568, 0, buffer, 54);   /* 'head' */
        put("FONTDATA "); num((int)all); put(" "); num((int)head); put(" "); hex(buffer, 54); flush();
    }
    /* a width asked for, as Welcome does for CONTENTS. */
    {
        U narrow = CreateFontA(-13, -6, 0, 0, 700, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
        check(narrow != 0, 9);
        SelectObject(dc, narrow);
        int size[2];
        check(GetTextMetricsA(dc, tm) && GetTextExtentPoint32A(dc, "CONTENTS", 8, size), 10);
        put("NARROW "); num(((int *)tm)[5]); put(" "); num(size[0]); flush();
        U narrower = CreateFontA(-13, -4, 0, 0, 700, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
        check(narrower != 0, 13);
        SelectObject(dc, narrower);
        check(GetTextMetricsA(dc, tm) && GetTextExtentPoint32A(dc, "CONTENTS", 8, size), 14);
        put("NARROWER "); num(((int *)tm)[5]); put(" "); num(size[0]); flush();
        SelectObject(dc, small);
        DeleteObject(narrower);
        U wide = CreateFontA(-13, 0, 0, 0, 700, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
        SelectObject(dc, wide);
        check(GetTextExtentPoint32A(dc, "CONTENTS", 8, size), 11);
        put("WIDE "); num(size[0]); flush();
        SelectObject(dc, small);
        DeleteObject(narrow); DeleteObject(wide);
    }
    /* Simulations on a face that has neither bold nor italic: Wingdings. */
    {
        int plain[1], boldw[1], gm[5];
        U base = CreateFontA(-20, 0, 0, 0, 400, 0, 0, 0, 2, 0, 0, 0, 0, "Wingdings");
        U bold = CreateFontA(-20, 0, 0, 0, 700, 0, 0, 0, 2, 0, 0, 0, 0, "Wingdings");
        U slant = CreateFontA(-20, 0, 0, 0, 400, 1, 0, 0, 2, 0, 0, 0, 0, "Wingdings");
        check(base && bold && slant, 15);
        SelectObject(dc, base);
        check(GetCharWidth32A(dc, 'o', 'o', plain) && GetTextMetricsA(dc, tm), 16);
        U upright = GetGlyphOutlineA(dc, 'o', 0, gm, 0, 0, identity);
        int upright_box = gm[0];
        put("SIMPLAIN "); num(plain[0]); put(" "); num(((int *)tm)[8]); put(" "); num(((int *)tm)[7]); put(" "); num(upright_box); flush();
        SelectObject(dc, bold);
        check(GetCharWidth32A(dc, 'o', 'o', boldw) && GetTextMetricsA(dc, tm), 17);
        put("SIMBOLD "); num(boldw[0]); put(" "); num(((int *)tm)[8]); put(" "); num(((int *)tm)[7]); flush();
        SelectObject(dc, slant);
        check(GetTextMetricsA(dc, tm), 18);
        (void)upright;
        GetGlyphOutlineA(dc, 'o', 0, gm, 0, 0, identity);
        put("SIMITALIC "); num(tm[48]); put(" "); num(gm[0]); flush();
        SelectObject(dc, small);
        DeleteObject(base); DeleteObject(bold); DeleteObject(slant);
    }
    /* Wingdings for the symbol mapping. */
    {
        U wing = CreateFontA(-2048, 0, 0, 0, 400, 0, 0, 0, 2, 0, 0, 0, 0, "Wingdings");
        check(wing && SelectObject(dc, wing), 12);
        outline(dc, 'o', "wing2048-o", 2);
        outline(dc, 'J', "wing2048-J", 2);
        SelectObject(dc, small);
        DeleteObject(wing);
    }
    SelectObject(dc, old_bitmap);
    DeleteObject(small);
    DeleteObject(section);
    DeleteDC(dc);
    ExitProcess(0);
}
