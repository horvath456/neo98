// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gdi.h"
#include <cstdio>
#include <climits>
#include <vector>
#include "../build/generated/text_vectors.h"
using namespace Gdi;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "GDI check failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static HBITMAP Bitmap(int w, int h, unsigned **pixels)
{
    BITMAPINFO info = {{40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0}};
    void *bits = nullptr;
    HBITMAP result = CreateDIBSection(0, &info, 0, &bits);
    *pixels = static_cast<unsigned *>(bits);
    return result;
}

static int TextAdapter()
{
    // Original driver results reached through actual GDI handles, COLORREF
    // realization, font selection, viewport transforms, clip and int lpDx.
    unsigned char pixels[25024];
    unsigned count = 0;
    for (const auto &v : TextVectors) {
        if (v[4] || (v[23] | v[24]) & 0xff000000) continue;
        bool spacing = false; for (unsigned i = 17; i <= 22; ++i) spacing |= v[i] != 0;
        if (spacing) continue;
        unsigned seed = v[25];
        for (auto &b : pixels) { seed = seed * 1664525u + 1013904223u; b = seed >> 24; }
        HDC dc = CreateSurfaceDC({pixels + 128 + (v[26] ? 47 * 516 : 0), 128, 48, v[26] ? -516 : 516, 32});
        HFONT font = CreateFontResource(v[0]); CHECK(dc && font);
        CHECK(SelectObject(dc, font)); CHECK(SaveDC(dc));
        CHECK(SetViewportOrgEx(dc, 7, -3));
        CHECK(IntersectClipRect(dc, (int)v[7] - 7, (int)v[8] + 3, (int)v[9] - 7, (int)v[10] + 3));
        CHECK(SetTextColor(dc, RGB(v[23] >> 16, v[23] >> 8, v[23])) != ~0u);
        CHECK(SetBkColor(dc, RGB(v[24] >> 16, v[24] >> 8, v[24])) != ~0u);
        CHECK(SetBkMode(dc, v[5]));
        RECT rect = {(int)v[12] - 7, (int)v[13] + 3, (int)v[14] - 7, (int)v[15] + 3};
        int dx[4096]; for (unsigned i = 0; v[16] && i < v[3]; ++i) dx[i] = TextAdvances[v[29] + i];
        CHECK(ExtTextOutA(dc, (int)v[1] - 7, (int)v[2] + 3, v[6], v[11] ? &rect : nullptr,
                          (const char *)TextStrings + v[28], v[3], v[16] ? dx : nullptr));
        unsigned hash = 2166136261u; for (auto b : pixels) hash = (hash ^ b) * 16777619u;
        if (hash != v[31]) { std::fprintf(stderr, "GDI text adapter original mismatch at case %td\n", &v - TextVectors); return 1; }
        CHECK(RestoreDC(dc, -1) && DeleteDC(dc) && DeleteObject(font));
        ++count;
    }
    CHECK(LiveObjects() == 1); ShutdownBitmaps(); CHECK(LiveObjects() == 0);
    HDC a = CreateCompatibleDC(), b = CreateCompatibleDC();
    HFONT big = CreateFontResource(5); CHECK(big && !CreateFontResource(6));
    HFONT normal = SelectObject(a, big); CHECK(normal && SelectObject(b, big));
    CHECK(!SelectObject(b, normal));
    SIZE extent, plain;
    CHECK(GetTextExtentPoint32A(a, "Win", 3, &plain) && plain.cy == 37);
    CHECK(SaveDC(a)); CHECK(SelectObject(a, normal)); CHECK(!DeleteObject(big));
    CHECK(GetTextExtentPoint32A(a, "Win", 3, &extent) && extent.cy == 13);
    CHECK(RestoreDC(a, -1)); CHECK(GetTextExtentPoint32A(a, "Win", 3, &extent) && extent.cx == plain.cx);
    CHECK(SetTextCharacterExtra(a, 3) == 0);
    CHECK(GetTextExtentPoint32A(a, "Win", 3, &extent) && extent.cx == plain.cx + 9);
    CHECK(SetTextCharacterExtra(a, 0) == 3);
    CHECK(GetTextExtentPoint32A(a, "A B C", 5, &plain));
    CHECK(SetTextJustification(a, 11, 2));
    CHECK(GetTextExtentPoint32A(a, "A B C", 5, &extent) && extent.cx == plain.cx + 11);
    CHECK(SetTextJustification(a, 0, 0));
    CHECK(GetTextExtentPoint32A(a, nullptr, 0, &extent) && !extent.cx && !extent.cy);
    CHECK(!GetTextExtentPoint32A(a, nullptr, 1, &extent));
    CHECK(!SetTextJustification(a, 12, 0) && SetTextJustification(a, 0, -1));
    DibText::Font malformed; const unsigned char bad[118] = {}; CHECK(!malformed.Load(bad, sizeof bad));
    CHECK(DeleteDC(a) && !DeleteObject(big) && DeleteDC(b) && DeleteObject(big));
    CHECK(LiveObjects() == 1); ShutdownBitmaps(); CHECK(LiveObjects() == 0);
    std::printf("PASS: %u original text results through GDI adapter; font lifetimes, measurement, character spacing and justification\n", count);
    return 0;
}

static int MonochromeText()
{
    // A one-bit toolbar mask must carry the same clipped glyphs as a black
    // and white window surface, including opaque rectangles and lpDx.
    for (bool bottom_up : {false, true}) for (int mode : {TRANSPARENT, OPAQUE})
        for (unsigned options : {0u, 2u, 4u, 6u}) for (bool inverse : {false, true}) {
            unsigned char mono[40 * 12];
            unsigned color[80 * 40];
            for (auto &v : mono) v = 0xa5;
            DibEng::Bitmap bits = {mono + (bottom_up ? 39 * 12 : 0), 80, 40,
                                  bottom_up ? -12 : 12, 1};
            for (int y = 0; y < 40; ++y) for (int x = 0; x < 80; ++x)
                color[y * 80 + x] = DibEng::ReadPixel(bits, x, y) ? 0xffffff : 0;
            HDC mask = CreateSurfaceDC(bits);
            HDC target = CreateSurfaceDC({(unsigned char *)color, 80, 40, 320, 32});
            CHECK(mask && target);
            RECT opaque = {5, 4, 67, 28};
            int dx[] = {8, 9, 7, 8, 10, 6, 9};
            for (HDC dc : {mask, target}) {
                CHECK(SetBkMode(dc, mode));
                CHECK(SetTextColor(dc, inverse ? 0xffffff : 0) != ~0u);
                CHECK(SetBkColor(dc, inverse ? 0 : 0xffffff) != ~0u);
                CHECK(IntersectClipRect(dc, 3, 2, 72, 31));
                CHECK(ExtTextOutA(dc, 1, 6, options, &opaque, "Forward", 7, dx));
            }
            unsigned changed = 0;
            for (int y = 0; y < 40; ++y) for (int x = 0; x < 80; ++x) {
                CHECK(DibEng::ReadPixel(bits, x, y) == (color[y * 80 + x] != 0));
                changed += color[y * 80 + x] != ((0xa5 & (0x80 >> (x & 7))) ? 0xffffffu : 0u);
            }
            CHECK(changed > 20);
            for (int y = 0; y < 40; ++y) CHECK(mono[y * 12 + 10] == 0xa5 && mono[y * 12 + 11] == 0xa5);
            CHECK(DeleteDC(mask) && DeleteDC(target));
        }
    ShutdownBitmaps();
    CHECK(LiveObjects() == 0);
    puts("PASS: monochrome text masks, clipping, background modes, lpDx and signed stride");
    return 0;
}

static int DeepText()
{
    // A DirectDraw surface of 8, 16 or 24 bits carries the glyphs of a 32-bit
    // surface, each colour realized to its format.
    for (int depth : {16, 24}) for (int mode : {TRANSPARENT, OPAQUE}) {
        unsigned reference[80 * 20];
        unsigned char deep[80 * 20 * 3];
        for (auto &v : reference) v = 0x00ff00;
        const unsigned green = depth == 16 ? 0x03e0u : 0x00ff00u;
        DibEng::Bitmap bits = {deep, 80, 20, 80 * depth / 8, depth};
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 80; ++x) DibEng::WritePixel(bits, x, y, green);
        HDC wide = CreateSurfaceDC({(unsigned char *)reference, 80, 20, 320, 32});
        HDC narrow = CreateSurfaceDC(bits);
        CHECK(wide && narrow);
        for (HDC dc : {wide, narrow}) {
            CHECK(SetBkMode(dc, mode));
            CHECK(SetTextColor(dc, RGB(255, 0, 0)) != ~0u);
            CHECK(SetBkColor(dc, RGB(0, 0, 255)) != ~0u);
            CHECK(ExtTextOutA(dc, 2, 3, 0, nullptr, "Loading", 7));
        }
        unsigned text = 0;
        for (int y = 0; y < 20; ++y) for (int x = 0; x < 80; ++x) {
            const unsigned want = reference[y * 80 + x];
            const unsigned expected = depth == 24 ? want
                : want == 0xff0000 ? 0x7c00u : want == 0x0000ff ? 0x001fu : green;
            CHECK(DibEng::ReadPixel(bits, x, y) == expected);
            text += want == 0xff0000;
        }
        CHECK(text > 20);
        CHECK(DeleteDC(wide) && DeleteDC(narrow));
    }
    ShutdownBitmaps();
    CHECK(LiveObjects() == 0);
    puts("PASS: text on 16 and 24-bit surfaces matches the 32-bit glyphs");
    return 0;
}

int main()
{
    unsigned failed = DibEng::OriginalSelfTest();
    if (failed) { std::fprintf(stderr, "DIBENG original mismatch at vector %u\n", failed); return 1; }
    unsigned text_actual[3];
    failed = DibText::OriginalSelfTest(text_actual);
    if (failed) { std::fprintf(stderr, "Text original mismatch at vector %u: return=%08x pixels=%08x state=%08x\n", failed, text_actual[0], text_actual[1], text_actual[2]); return 1; }
    CHECK(LiveObjects() == 0);
    CHECK(TextAdapter() == 0);
    CHECK(MonochromeText() == 0);
    CHECK(DeepText() == 0);
    HDC a = CreateCompatibleDC(), b = CreateCompatibleDC();
    CHECK(a && b && a != b);
    unsigned *pixels, *other;
    HBITMAP bitmap = Bitmap(19, 13, &pixels), second = Bitmap(19, 13, &other);
    CHECK(bitmap && second);
    HGDIOBJ default_a = SelectObject(a, bitmap), default_b = SelectObject(b, second);
    CHECK(default_a && default_b);
    CHECK(!SelectObject(b, bitmap));
    CHECK(!DeleteObject(bitmap));
    HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
    HGDIOBJ default_brush = SelectObject(a, red);
    CHECK(default_brush && !DeleteObject(red));
    CHECK(PatBlt(a, 0, 0, 19, 13, PATCOPY));
    for (int i = 0; i < 247; ++i) CHECK(pixels[i] == 0xff0000);
    CHECK(SaveDC(a) == 1);
    CHECK(IntersectClipRect(a, 2, 2, 17, 11) == 2);
    CHECK(ExcludeClipRect(a, 6, 4, 12, 9) == 3);
    CHECK(PatBlt(a, -50, -50, 100, 100, WHITENESS));
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x) {
        bool visible = x >= 2 && x < 17 && y >= 2 && y < 11 && !(x >= 6 && x < 12 && y >= 4 && y < 9);
        CHECK(pixels[y * 19 + x] == (visible ? 0xffffffff : 0xff0000));
    }
    CHECK(RestoreDC(a, -1));
    CHECK(!RestoreDC(a, 0) && !RestoreDC(a, -1));
    CHECK(SaveDC(a) == 1);
    CHECK(SelectObject(a, default_a) == bitmap);
    CHECK(!DeleteObject(bitmap)); // saved selection still pins the bitmap
    CHECK(!SelectObject(b, bitmap));
    CHECK(RestoreDC(a, 1));
    CHECK(SelectObject(a, bitmap) == bitmap);

    for (int i = 0; i < 247; ++i) { pixels[i] = i; other[i] = 0x98765432; }
    CHECK(SetViewportOrgEx(a, 2, 1));
    CHECK(SetViewportOrgEx(b, 1, 2));
    CHECK(BitBlt(b, -3, -3, 10, 10, a, 0, 0, SRCCOPY));
    // Destination starts at (-2,-1), so clipping advances source (2,1) to (4,2).
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x)
        CHECK(other[y * 19 + x] == (x < 8 && y < 9 ? (unsigned)((y + 2) * 19 + x + 4) : 0x98765432));
    CHECK(SetViewportOrgEx(a, 0, 0) && SetViewportOrgEx(b, 0, 0));
    // The original coordinate engine narrows to signed WORDs. INT_MAX is -1
    // and INT_MIN is zero; check the actual clipped copies after that mapping.
    for (int i = 0; i < 247; ++i) { pixels[i] = 0x55667788; other[i] = 0x100000 + i; }
    CHECK(BitBlt(a, INT_MAX, 0, 10, 10, b, 0, 0, SRCCOPY));
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x)
        CHECK(pixels[y * 19 + x] == (x < 9 && y < 10 ? other[y * 19 + x + 1] : 0x55667788));
    CHECK(BitBlt(a, 0, 0, 10, 10, b, INT_MIN, 0, SRCCOPY));
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x)
        CHECK(pixels[y * 19 + x] == (x < 10 && y < 10 ? other[y * 19 + x] : 0x55667788));

    // Complex destination clipping plus an overlapping self-copy must read
    // source pixels as they were before any of the clipped pieces was drawn.
    for (int i = 0; i < 247; ++i) pixels[i] = i;
    CHECK(SaveDC(a)); CHECK(ExcludeClipRect(a, 6, 4, 12, 9));
    CHECK(BitBlt(a, 3, 2, 14, 10, a, 0, 0, SRCCOPY));
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x) {
        bool copied = x >= 3 && x < 17 && y >= 2 && y < 12 && !(x >= 6 && x < 12 && y >= 4 && y < 9);
        CHECK(pixels[y * 19 + x] == (unsigned)(copied ? (y - 2) * 19 + x - 3 : y * 19 + x));
    }
    CHECK(RestoreDC(a, -1));
    CHECK(SaveDC(a)); CHECK(IntersectClipRect(a, 2, 2, 7, 7));
    for (int i = 0; i < 247; ++i) pixels[i] = 0xabcdef;
    CHECK(SetTextColor(a,0x02010203)!=~0u&&GetTextColor(a)==0x010203);
    CHECK(SetBkColor(a,0x02040506)!=~0u&&GetBkColor(a)==0x040506);
    for(int i=0;i<247;++i)pixels[i]=0xffffff;
    CHECK(SetBkMode(a,TRANSPARENT)&&SetTextColor(a,0x02000000)!=~0u&&TextOutA(a,0,0,"C:",2));
    bool tagged_text=false;for(int i=0;i<247;++i)tagged_text|=pixels[i]!=0xffffff;CHECK(tagged_text);
    for (int i = 0; i < 247; ++i) pixels[i] = 0xabcdef;
    CHECK(SetBkMode(a, TRANSPARENT)); CHECK(SetTextColor(a, RGB(255, 255, 255)) != ~0u);
    CHECK(TextOutA(a, 0, 0, "Windows", 7));
    bool drew_text = false;
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x) {
        if (pixels[y * 19 + x] != 0xabcdef) { CHECK(x >= 2 && x < 7 && y >= 2 && y < 7); drew_text = true; }
    }
    CHECK(drew_text && RestoreDC(a, -1));

    unsigned *pattern_pixels;
    HBITMAP pattern_bitmap = Bitmap(8, 8, &pattern_pixels);
    for (unsigned i = 0; i < 64; ++i) pattern_pixels[i] = i * 0x10203;
    HBRUSH pattern_brush = CreatePatternBrush(pattern_bitmap);
    CHECK(pattern_brush && DeleteObject(pattern_bitmap)); // brush owns its pattern
    CHECK(SelectObject(a, pattern_brush) == red);
    CHECK(PatBlt(a, 0, 0, 19, 13, PATCOPY));
    for (int y = 0; y < 13; ++y) for (int x = 0; x < 19; ++x)
        CHECK(pixels[y * 19 + x] == (unsigned)(((y & 7) * 8 + (x & 7)) * 0x10203));
    CHECK(SelectObject(a, default_brush) == pattern_brush);
    CHECK(DeleteObject(pattern_brush) && DeleteObject(red));
    CHECK(!SelectObject(a, pattern_brush));
    CHECK(SelectObject(a, default_a) == bitmap && SelectObject(b, default_b) == second);
    CHECK(DeleteObject(bitmap) && DeleteObject(second));
    CHECK(DeleteDC(a) && DeleteDC(b) && !DeleteDC(a));
    CHECK(LiveObjects() == 1); ShutdownBitmaps(); CHECK(LiveObjects() == 0);
    std::vector<HDC> contexts;
    while (HDC dc = CreateCompatibleDC()) contexts.push_back(dc);
    unsigned count = contexts.size();
    CHECK(count > 10 && LiveObjects() == count * 5 + 1);
    while (count) CHECK(DeleteDC(contexts[--count]));
    CHECK(LiveObjects() == 1); ShutdownBitmaps(); CHECK(LiveObjects() == 0);
    std::printf("PASS: %u original DIBENG vectors, 256 raster truth tables; GDI handles, selections, saved state, clipping, overlap, font, pattern, exhaustion\n", DibEng::OriginalTestCount());
    std::printf("PASS: %u original DIB_EXTTEXTOUT calls, six FNT fonts, whole buffers and DRAWMODE state\n", DibText::OriginalTestCount());
}
