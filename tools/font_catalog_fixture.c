/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The fonts of the installation, the stock fonts and GDI's text simulations,
   checked through the public API. The command line carries what the test read
   from the files themselves:
     FONTCAT.EXE height ascent average ttTop ttThick|Family|Family|...
   height/ascent/average are VGASYS.FON's dfPixHeight, dfAscent and dfAvgWidth,
   ttTop/ttThick the underline row and thickness of Arial at 13 ppem as GDI scales them from the VDMX, OS/2 and post tables, and the families
   those of the raster files the registry key and SYSTEM.INI name. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API const char *GetCommandLineA(void);
API U CreateCompatibleDC(U);
API U DeleteDC(U);
API U SelectObject(U, U);
API U GetStockObject(U);
API U GetTextMetricsA(U, void *);
API U GetTextFaceA(U, U, char *);
API U GetObjectA(U, U, void *);
API U EnumFontsA(U, const char *, void *, U);
API U CreateFontA(int, int, int, int, int, U, U, U, U, U, U, U, U, const char *);
API U DeleteObject(U);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U SetBkMode(U, U);
API U SetTextColor(U, U);
API U TextOutA(U, int, int, const char *, int);
API U PatBlt(U, int, int, int, int, U);
API U GetTextExtentPoint32A(U, const char *, int, int *);
API U GetRasterizerCaps(void *, U);
API U AddFontResourceA(const char *);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static int equal(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}
static int s32(const unsigned char *p) { return (int)(p[0] | (U)p[1] << 8 | (U)p[2] << 16 | (U)p[3] << 24); }

#define W 256
#define H 128
static U dc, *bits;
static int ink(int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && (bits[y * W + x] & 0xffffff) == 0; }
static void clear(void) { PatBlt(dc, 0, 0, W, H, 0x00ff0062); }

/* Face names seen by EnumFontsA(NULL). */
static char seen[48][32];
static U seen_count, truetype_seen;
static U __stdcall family(const unsigned char *lf, const unsigned char *tm, U type, U param)
{
    (void)tm; (void)param;
    if (seen_count < 48) {
        U i = 0;
        for (; i < 31 && lf[28 + i]; ++i) seen[seen_count][i] = lf[28 + i];
        seen[seen_count][i] = 0;
        ++seen_count;
    }
    if (type == 4) ++truetype_seen;
    return 1;
}
static U arial_records, arial_type;
static U __stdcall arial(const unsigned char *lf, const unsigned char *tm, U type, U param)
{
    (void)tm; (void)param;
    check(equal((const char *)lf + 28, "Arial"), 30);
    ++arial_records; arial_type |= type;
    return 1;
}

static const char *number(const char *p, int *out)
{
    int v = 0, sign = 1;
    while (*p == ' ') ++p;
    if (*p == '-') { sign = -1; ++p; }
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *out = v * sign;
    return p;
}

/* The line rule of GDI for a raster font. */
static void raster_line(const unsigned char *tm, int strikeout, int *top, int *thickness)
{
    int h = s32(tm), a = s32(tm + 4), il = s32(tm + 12), t = h > 12 ? h / 12 : 1, pos;
    if (!strikeout) pos = a + t + 1 > h ? h + (-1 - t) / 2 : t / 2 + a + 1;
    else pos = il + (2 * (a - il)) / 3;
    *top = pos - t / 2; *thickness = t;
}
/* Exactly the rows [top, top+thickness) are black from x0 to x1 - 1. */
static void only_line(int x0, int x1, int y, int top, int thickness, U code)
{
    for (int row = 0; row < H; ++row)
        for (int x = 0; x < W; ++x) {
            const int want = row >= y + top && row < y + top + thickness && x >= x0 && x < x1;
            check(ink(x, row) == want, code);
        }
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    while (*line && *line != ' ') ++line;
    int height, ascent, average, tt_top, tt_thick;
    line = number(line, &height); line = number(line, &ascent); line = number(line, &average);
    line = number(line, &tt_top); line = number(line, &tt_thick);
    while (*line == ' ') ++line;

    dc = CreateCompatibleDC(0);
    check(dc != 0, 1);
    struct { U size; int w, h; unsigned short planes, bits; U compression, image, xp, yp, used, important; } info =
        {40, W, -H, 1, 32, 0, 0, 0, 0, 0, 0};
    void *pixels = 0;
    U section = CreateDIBSection(dc, &info, 0, &pixels, 0, 0);
    check(section && pixels, 2);
    bits = (U *)pixels;
    U old_bitmap = SelectObject(dc, section);
    check(old_bitmap != 0, 3);

    /* Group A: the System font comes from VGASYS.FON, and a new DC starts
       with it (GDI selects the stock SYSTEM_FONT). */
    unsigned char tm[56];
    char face[32];
    check(GetTextMetricsA(dc, tm) && s32(tm) == height && s32(tm + 4) == ascent && s32(tm + 20) == average, 10);
    check(GetTextFaceA(dc, 32, face) && equal(face, "System"), 11);
    U system = GetStockObject(13), previous = SelectObject(dc, system);
    check(previous && GetTextMetricsA(dc, tm) && s32(tm) == height && s32(tm + 4) == ascent &&
          s32(tm + 20) == average, 12);
    check(SelectObject(dc, GetStockObject(16)) && GetTextFaceA(dc, 32, face) && equal(face, "Fixedsys"), 13);
    check(SelectObject(dc, GetStockObject(10)) && GetTextFaceA(dc, 32, face) && equal(face, "Terminal"), 14);
    check(SelectObject(dc, GetStockObject(11)) && GetTextFaceA(dc, 32, face) && equal(face, "Courier"), 15);
    check(SelectObject(dc, GetStockObject(17)) && GetTextFaceA(dc, 32, face) && equal(face, "MS Sans Serif"), 16);
    unsigned char lf[60];
    check(GetObjectA(GetStockObject(11), 60, lf) == 60 && s32(lf) == 12 && s32(lf + 4) == 9 &&
          equal((const char *)lf + 28, "Courier"), 17);
    SelectObject(dc, system);

    /* EnumFonts lists every raster family the installation names. */
    check(EnumFontsA(dc, 0, (void *)family, 0) == 1, 20);
    for (const char *p = line; *p;) {
        char want[32]; U n = 0;
        while (*p && *p != '|' && n < 31) want[n++] = *p++;
        want[n] = 0;
        if (*p == '|') ++p;
        if (!n) continue;
        U found = 0;
        for (U i = 0; i < seen_count; ++i) found |= equal(seen[i], want);
        check(found, 21);
    }
    check(truetype_seen > 0, 22);
    check(EnumFontsA(dc, "Arial", (void *)arial, 0) == 1 && arial_records >= 1 && arial_type == 4, 23);
    unsigned short caps[3] = {0, 0, 0};
    check(GetRasterizerCaps(caps, 6) && caps[0] == 6 && caps[1] == 3, 24);

    SetBkMode(dc, 1);
    SetTextColor(dc, 0);

    /* Group B: underline and strikeout of a raster font after GDI. */
    for (U which = 0; which < 2; ++which) {
        U f = CreateFontA(13, 0, 0, 0, 400, 0, which == 0, which == 1, 0, 0, 0, 0, 0, "MS Sans Serif");
        check(f && SelectObject(dc, f), 40);
        check(GetTextMetricsA(dc, tm) && tm[49 + which] == 1, 41);
        int size[2], top, thick;
        check(GetTextExtentPoint32A(dc, "    ", 4, size), 42);
        raster_line(tm, which, &top, &thick);
        clear();
        check(TextOutA(dc, 10, 20, "    ", 4), 43);
        only_line(10, 10 + size[0], 20, top, thick, 44 + which);
        SelectObject(dc, system);
        DeleteObject(f);
    }
    /* The System font itself, bold and taller than twelve rows. */
    {
        U f = CreateFontA(16, 0, 0, 0, 700, 0, 1, 0, 0, 0, 0, 0, 0, "System");
        check(f && SelectObject(dc, f) && GetTextMetricsA(dc, tm) && s32(tm) == height, 50);
        int size[2], top, thick;
        check(GetTextExtentPoint32A(dc, "   ", 3, size), 51);
        raster_line(tm, 0, &top, &thick);
        clear();
        check(TextOutA(dc, 30, 40, "   ", 3), 52);
        only_line(30, 30 + size[0], 40, top, thick, 53);
        SelectObject(dc, system);
        DeleteObject(f);
    }
    /* Italic: bands of two rows, band r moved right by (h - r - 1) / 2; the overhang is (h - 1) / 2. */
    {
        U upright = CreateFontA(16, 0, 0, 0, 700, 0, 0, 0, 0, 0, 0, 0, 0, "System");
        U slanted = CreateFontA(16, 0, 0, 0, 700, 1, 0, 0, 0, 0, 0, 0, 0, "System");
        check(upright && slanted, 60);
        clear();
        SelectObject(dc, upright);
        int size[2];
        check(GetTextExtentPoint32A(dc, "HIM", 3, size) && TextOutA(dc, 10, 10, "HIM", 3), 61);
        SelectObject(dc, slanted);
        check(GetTextMetricsA(dc, tm) && tm[48] == 1 && s32(tm + 32) == (s32(tm) - 1) / 2, 62);
        check(TextOutA(dc, 10, 60, "HIM", 3), 63);
        const int h = s32(tm);
        U black = 0;
        for (int row = 0; row < h; ++row) {
            const int shift = (h - (row & ~1) - 1) / 2;
            for (int x = 0; x < size[0]; ++x) {
                check(ink(10 + x, 10 + row) == ink(10 + x + shift, 60 + row), 64);
                black += ink(10 + x, 10 + row);
            }
        }
        check(black > 20, 65);
        /* An italic underline starts where its row of the slanted text starts. */
        U both = CreateFontA(16, 0, 0, 0, 700, 1, 1, 0, 0, 0, 0, 0, 0, "System");
        SelectObject(dc, both);
        int top, thick;
        raster_line(tm, 0, &top, &thick);
        clear();
        check(GetTextExtentPoint32A(dc, "   ", 3, size) && TextOutA(dc, 10, 30, "   ", 3), 66);
        only_line(10 + (h - top) / 2, 10 + (h - top) / 2 + size[0], 30, top, thick, 67);
        SelectObject(dc, system);
        DeleteObject(upright); DeleteObject(slanted); DeleteObject(both);
    }

    /* Group C: Wingdings and Arial come from the TrueType files. */
    {
        U f = CreateFontA(-13, 0, 0, 0, 400, 0, 0, 0, 2, 0, 0, 0, 0, "Wingdings");
        check(f && SelectObject(dc, f), 70);
        check(GetTextFaceA(dc, 32, face) && equal(face, "Wingdings"), 71);
        check(GetTextMetricsA(dc, tm) && tm[52] == 2 && (tm[51] & 4), 72);
        clear();
        check(TextOutA(dc, 10, 10, "o", 1), 73);
        /* Wingdings "o" is the empty box Welcome draws: a closed square
           outline with an empty inside. */
        int left = W, right = -1, topmost = H, bottom = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (ink(x, y)) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                    if (y < topmost) topmost = y;
                    if (y > bottom) bottom = y;
                }
        check(right - left >= 5 && bottom - topmost >= 5, 74);
        for (int x = left; x <= right; ++x) check(ink(x, topmost) && ink(x, bottom), 75);
        for (int y = topmost; y <= bottom; ++y) check(ink(left, y) && ink(right, y), 76);
        check(!ink((left + right) / 2, (topmost + bottom) / 2), 77);
        SelectObject(dc, system);
        DeleteObject(f);
        U a = CreateFontA(-13, 0, 0, 0, 400, 0, 1, 0, 0, 0, 0, 0, 0, "Arial");
        check(a && SelectObject(dc, a) && GetTextFaceA(dc, 32, face) && equal(face, "Arial"), 78);
        check(GetTextMetricsA(dc, tm) && (tm[51] & 4), 79);
        int size[2];
        clear();
        check(GetTextExtentPoint32A(dc, "  ", 2, size) && TextOutA(dc, 10, 20, "  ", 2), 80);
        only_line(10, 10 + size[0], 20, tt_top, tt_thick, 81);
        SelectObject(dc, system);
        DeleteObject(a);
    }
    SelectObject(dc, old_bitmap);
    DeleteObject(section);
    DeleteDC(dc);
    /* AddFontResource installs a raster font file of the
       installation that the registry does not name and answers its faces;
       a second call answers them again, a missing file zero. */
    {
        const U first = AddFontResourceA("8514FIX.FON");
        check(first != 0, 900);
        check(AddFontResourceA("8514FIX.FON") == first, 901);
        check(AddFontResourceA("C:\\NOFONT.FON") == 0, 902);
    }
    ExitProcess(0);
}
