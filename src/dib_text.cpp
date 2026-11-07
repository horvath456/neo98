// SPDX-License-Identifier: GPL-2.0-or-later
#include "dib_text.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "gdi_font_catalog.h"
#else
#include "../build/generated/text_fonts.h"
#endif
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/text_vectors.h"
#endif

namespace DibText {
namespace {
unsigned Word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
unsigned Dword(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }
int S16(int n) { return (n & 0x7fff) - (n & 0x8000); }
bool Overflow(int n) { return n < -32768 || n > 32767; }
bool Intersect(Rect &r, const Rect &a, const Rect &b) { return Win98::IntersectRect(&r, &a, &b); }

// Windows 98 chooses spacing paths. Inactive justification locals are zeroed
// here, unlike the original's uninitialized stack slots.
struct Spacing {
    int extra, each = 0, error = 0, remainder = 0, breaks = 0;
    bool justify, distribute, slow;
    explicit Spacing(const DrawMode &dm, const short *dx)
        : extra(dm.char_extra), justify(dm.total_break_extra != 0),
          distribute(justify && dm.break_remainder != 0),
          slow(dx || dm.char_extra < 0 || dm.total_break_extra < 0)
    {
        if (justify) each = dm.break_extra;
        if (distribute) { error = dm.break_error; remainder = dm.break_remainder; breaks = dm.break_count; }
    }
    // Signed remainder distribution.
    int Extra(bool is_break)
    {
        int result = extra;
        if (is_break && justify) {
            result = S16(result + each);
            if (distribute) {
                error = S16(error - remainder);
                if (remainder >= 0 ? error <= 0 : error >= 0) {
                    error = S16(error + (remainder >= 0 ? breaks : -breaks));
                    result = S16(result + (remainder >= 0 ? 1 : -1));
                }
            }
        }
        return result;
    }
};

// This returns the rightmost glyph/advance extent for negative
// spacing and lpDx, which can differ from the final pen position.
unsigned Measure(const Font &f, const unsigned char *text, unsigned n, DrawMode &dm, const short *dx)
{
    Spacing spacing(dm, dx);
    int width = 0;
    const unsigned break_index = f.data[0x62];
    if (spacing.slow) {
        int position = 0;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned index = f.Index(text[i]);
            const int glyph = f.Width(index);
            int extra = spacing.Extra(index == break_index);
            if (dx) extra = S16(extra + dx[i] - glyph);
            if (Overflow(position + glyph)) { width = 32767; break; }
            position += glyph;
            if (position > width) width = position;
            if (Overflow(position + extra)) { width = 32767; break; }
            position += extra;
            if (position > width) width = position;
        }
        dm.break_error = spacing.error;
    } else {
        const int fixed = Word(f.data + 0x56);
        if (fixed) {
            width = (int)n * S16(fixed + spacing.extra);
            if (Overflow(width)) return (f.Height() << 16) | 32767;
        } else {
            // The proportional path does not check IMUL's overflow flag.
            width = S16(spacing.extra * (int)n);
            for (unsigned i = 0; i < n; ++i) {
                const int next = width + f.Width(f.Index(text[i]));
                if (Overflow(next)) return (f.Height() << 16) | 32767;
                width = next;
            }
        }
        if (spacing.justify && (spacing.each || spacing.distribute)) {
            unsigned breaks = 0;
            for (unsigned i = 0; i < n; ++i) {
                const unsigned relative = (unsigned char)(text[i] - f.data[0x5f]);
                // Windows 98 compares the break index with itself. Consequently the
                // fast path also counts undefined characters as breaks.
                breaks += relative == break_index || relative > (unsigned)(f.data[0x60] - f.data[0x5f]);
            }
            if (breaks) {
                const int next = width + S16(spacing.each * (int)breaks);
                if (Overflow(next)) return (f.Height() << 16) | 32767;
                width = next;
                if (spacing.distribute) {
                    for (unsigned i = 0; i < breaks; ++i) {
                        spacing.error = S16(spacing.error - spacing.remainder);
                        if (spacing.error <= 0) {
                            spacing.error = S16(spacing.error + spacing.breaks);
                            if (++width > 32767) { width = 32767; break; }
                        }
                    }
                    dm.break_error = spacing.error;
                }
            }
        }
    }
    return (f.Height() << 16) | (width & 65535);
}
void Fill(DibEng::Bitmap &b, const Rect &r, unsigned color)
{
    DibEng::BitBlt(b, r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, 0, 0, 0xf00021, {color});
}
unsigned Hash(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
}

bool Font::Load(const unsigned char *p, unsigned bytes)
{
    data = nullptr; length = 0;
    if (!p || bytes < 118 || (Word(p) != 0x200 && Word(p) != 0x300) ||
        Dword(p + 2) > bytes || Word(p + 0x42) || !Word(p + 0x58) ||
        Word(p + 0x58) > 128 || p[0x60] < p[0x5f]) return false;
    const unsigned count = p[0x60] - p[0x5f] + 1;
    const unsigned table = Word(p) == 0x200 ? 118 : 148, entry = Word(p) == 0x200 ? 4 : 6;
    if (p[0x61] >= count || p[0x62] >= count || table + count * entry > bytes) return false;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned width = Word(p + table + entry * i);
        const unsigned offset = entry == 4 ? Word(p + table + entry * i + 2) : Dword(p + table + entry * i + 2);
        const unsigned size = ((width + 7) / 8) * Word(p + 0x58);
        if (width > 256 || offset > bytes || size > bytes - offset) return false;
    }
    data = p; length = bytes; return true;
}
bool Font::FaceName(char (&output)[32]) const
{
    if(!data || length<109)return false;
    const unsigned offset=Dword(data+105);if(!offset || offset>=length)return false;
    for(unsigned i=0;i<32;++i){if(i>=length-offset)return false;output[i]=data[offset+i];if(!output[i])return true;}
    return false;
}
unsigned Font::Height() const { return Word(data + 0x58); }
unsigned Font::Points() const { return Word(data + 0x44); }
unsigned Font::Ascent() const { return Word(data + 0x4a); }
unsigned Font::Index(unsigned char code) const
{
    const unsigned relative = (unsigned char)(code - data[0x5f]);
    return relative > (unsigned)(data[0x60] - data[0x5f]) ? data[0x61] : relative;
}
unsigned Font::Width(unsigned index) const { return Word(data + (Word(data) == 0x200 ? 118 + index * 4 : 148 + index * 6)); }
unsigned Font::Bits(unsigned index) const
{
    return Word(data) == 0x200 ? Word(data + 120 + index * 4) : Dword(data + 150 + index * 6);
}
namespace { OutlineText outline_text = nullptr; }
void SetOutlineText(OutlineText hook) { outline_text = hook; }
#ifdef NEO98_RUNTIME_CONTENT
// The host's catalogue owns the FNT resources read from C:\WINDOWS\FONTS.
// Standalone tests keep their generated six-font fixture below.
unsigned FontCount()
{
    unsigned count = 0;
    for (unsigned i = 0; i < GdiFonts::Count(); ++i)
        if (GdiFonts::SameFace(GdiFonts::At(i)->file, "SSERIFE.FON")) ++count;
    return count;
}
Font OriginalFont(unsigned index)
{
    for (unsigned i = 0; i < GdiFonts::Count(); ++i)
        if (GdiFonts::SameFace(GdiFonts::At(i)->file, "SSERIFE.FON") && !index--)
            return GdiFonts::At(i)->font;
    return {};
}
#else
unsigned FontCount() { return sizeof(TextFontData) / sizeof(TextFontData[0]); }
Font OriginalFont(unsigned i)
{
    Font f;
    if (i < FontCount()) f.Load(TextFontData[i], TextFontLengths[i]);
    return f;
}
#endif

unsigned ExtTextOut(DibEng::Bitmap *bitmap, int x, int y, const Rect &clip,
                    const unsigned char *text, int count, const Font &font, DrawMode &dm,
                    const short *dx, const Rect *opaque, unsigned options)
{
    if (!font.data || (!text && count) || count < -4096 || count > 4096 ||
        options & ~0x16u || (dm.background_mode != 1 && dm.background_mode != 2)) return 0x80000000;
    if(options&0x10)
        return font.outline&&outline_text?outline_text(bitmap,x,y,clip,text,count,font,dm,dx,opaque,options):0x80000000u;
    if (count < 0) return Measure(font, text, -count, dm, dx);
    if (font.outline)
        return outline_text ? outline_text(bitmap, x, y, clip, text, count, font, dm, dx, opaque, options) : 0x80000000;
    if (!bitmap || !DibEng::Valid(*bitmap) || (bitmap->depth != 32 && bitmap->depth != 24 && bitmap->depth != 16 && bitmap->depth != 8 &&
         bitmap->depth != 1) ||
        Overflow(x) || Overflow(y)) return 0x80000000;
    Rect bounded;
    if (!Intersect(bounded, clip, {0, 0, bitmap->width, bitmap->height})) return 0;
    // Optional clipping and opaque rectangle.
    if ((options & 4) && opaque && !Intersect(bounded, bounded, *opaque)) return 0;
    if ((options & 2) && opaque) {
        Rect fill;
        if (Intersect(fill, bounded, *opaque)) Fill(*bitmap, fill, dm.background);
    }
    if (!count) return 0;
    Spacing spacing(dm, dx);
    DrawMode measuring = dm;
    const unsigned extent = Measure(font, text, count, measuring, dx);
    // resets the measuring pass's justification error before rendering.
    dm.break_error = spacing.error;
    Rect area;
    if (!Intersect(area, bounded, {x, y, S16(x + (extent & 65535)), S16(y + font.Height())})) return 0;
    const int width = area.right - area.left, height = area.bottom - area.top;
    const unsigned stride = (width + 7) / 8;
    // Compose all glyphs into an OR mask before applying colors.
    // This implementation uses a compact bounded mask.
    auto *mask = new unsigned char[stride * height]{};
    for (int i = 0, pen = x; i < count; ++i) {
        const unsigned index = font.Index(text[i]), gw = font.Width(index), bits = font.Bits(index);
        Rect visible;
        if (!Overflow(pen + (int)gw) && Intersect(visible, area, {pen, y, pen + (int)gw, y + (int)font.Height()})) {
            for (int row = visible.top; row < visible.bottom; ++row)
                for (int col = visible.left; col < visible.right; ++col) {
                    const unsigned gx = col - pen;
                    if (font.data[bits + (gx / 8) * font.Height() + row - y] & (0x80 >> (gx & 7)))
                        mask[(row - area.top) * stride + (col - area.left) / 8] |= 0x80 >> ((col - area.left) & 7);
                }
        }
        pen = S16(pen + (dx ? dx[i] : (int)gw) + spacing.Extra(index == font.data[0x62]));
    }
    // Expand the glyph mask using already realized device colors. The same
    // mask supplies monochrome text surfaces, 32-bit window surfaces and the
    // 8, 16 and 24-bit surfaces of DirectDraw.
    // DIB_EXTTEXTOUT does not use rop2.
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
        const bool foreground = mask[row * stride + col / 8] & (0x80 >> (col & 7));
        if (foreground || dm.background_mode == 2)
            DibEng::WritePixel(*bitmap, area.left + col, area.top + row, foreground ? dm.foreground : dm.background);
    }
    delete[] mask;
    return 0;
}

#ifndef NEO98_RUNTIME_CONTENT
unsigned OriginalTestCount() { return sizeof(TextVectors) / sizeof(TextVectors[0]); }
unsigned OriginalSelfTest(unsigned *actual)
{
    unsigned char dest[25024];
    Font fonts[6]; for (unsigned i = 0; i < 6; ++i) fonts[i] = OriginalFont(i);
    for (unsigned i = 0; i < OriginalTestCount(); ++i) {
        const auto &v = TextVectors[i];
        unsigned seed = v[25];
        for (auto &byte : dest) { seed = seed * 1664525u + 1013904223u; byte = seed >> 24; }
        DibEng::Bitmap bitmap = {dest + 128 + (v[26] ? 47 * 516 : 0), 128, 48, v[26] ? -516 : 516, 32};
        DrawMode dm;
        dm.rop2 = v[27]; dm.background_mode = v[5]; dm.foreground = v[23]; dm.background = v[24];
        dm.total_break_extra = v[17]; dm.break_extra = v[18]; dm.break_error = v[19];
        dm.break_remainder = v[20]; dm.break_count = v[21]; dm.char_extra = v[22];
        const Rect clip = {(int)v[7], (int)v[8], (int)v[9], (int)v[10]}, opaque = {(int)v[12], (int)v[13], (int)v[14], (int)v[15]};
        const unsigned result = ExtTextOut(&bitmap, (int)v[1], (int)v[2], clip, TextStrings + v[28],
            v[4] ? -(int)v[3] : (int)v[3], fonts[v[0]], dm, v[16] ? TextAdvances + v[29] : nullptr, v[11] ? &opaque : nullptr, v[6]);
        const unsigned pixels = Hash(dest, sizeof dest), state = Hash((const unsigned char *)&dm, sizeof dm);
        if (result != v[30] || pixels != v[31] || state != v[32]) {
            if (actual) { actual[0] = result; actual[1] = pixels; actual[2] = state; }
            return i + 1;
        }
    }
    return 0;
}
#endif
}
