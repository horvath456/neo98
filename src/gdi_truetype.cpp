// SPDX-License-Identifier: GPL-2.0-or-later
// TrueType realization through the Windows 98 font-driver interface. See
// gdi_truetype.h for font state and rasterization providers.
#include "gdi_truetype.h"
#include "truetype_machine.h"
#include "gdi.h"
#include "win98_engine_font.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/font_charset_tables.h"
#endif
#include <stdlib.h>
#include <string.h>

namespace GdiTrueType
{
// One text-output glyph of a realized font, fetched on first use.
struct Glyph
{
    bool ready = false, ok = false;
    GlyphImage image;
    unsigned char *storage = nullptr;
};

namespace
{
namespace M = TrueTypeMachine;

// The display: 96 pixels per inch both ways and no spot size, which GDI
// replaces with B505h.
constexpr int LogPixels = 96;
constexpr U Spot = 0xb505;

constexpr U FaceCapacity = 128, FontCapacity = 256;
Face faces[FaceCapacity];
U face_count = 0;
Font *fonts[FontCapacity];
U font_count = 0;

int Word(const unsigned char *p, U at) { return short(p[at] | U(p[at + 1]) << 8); }
U UWord(const unsigned char *p, U at) { return p[at] | U(p[at + 1]) << 8; }
void Put16(unsigned char *p, U at, U v) { p[at] = v & 255; p[at + 1] = (v >> 8) & 255; }
void Put32(unsigned char *p, U at, U v) { Put16(p, at, v & 0xffff); Put16(p, at + 2, v >> 16); }

// MULDIV of GDI.EXE : 16-bit operands, the magnitude rounded half up,
// 7FFFh (8000h with a negative sign) when the quotient leaves 15 bits or the
// divisor is zero.
int MulDiv16(int a, int b, int c)
{
    a = short(a), b = short(b), c = short(c);
    const bool negative = (a < 0) != (b < 0) != (c < 0);
    const unsigned x = unsigned(a < 0 ? -a : a), y = unsigned(b < 0 ? -b : b), z = unsigned(c < 0 ? -c : c) & 0xffff;
    const unsigned long long p = (unsigned long long)(x & 0xffff) * (y & 0xffff) + (z >> 1);
    if (!z || (p >> 16) >= z || p / z >= 0x8000)
        return negative ? -0x8000 : 0x7fff;
    const int q = int(p / z);
    return negative ? -q : q;
}

unsigned char U8(U v) { return (unsigned char)v; }

// One driver call; a stopped machine answers ~0u like a failing entry.
template <class... T> U Call(U entry, T... values)
{
    const U arguments[] = {U(values)...};
    bool ok = false;
    const U result = M::Call(entry, arguments, U(sizeof...(values)), ok);
    return ok ? result : ~0u;
}

// ---- the driver's single context ------------------------
// GDI keeps one driver context at a time in DGROUP 3EAh, for the font whose
// directory index it remembers in 3E0h, and sets the transformation again when
// ppem (3D2h), resolution (3D4h/3D6h), escapement (3E2h), simulated italic
// (3E4h), width factor (3E6h), spot size (3D8h/3DCh) or a MAT2 differ.
struct Current
{
    const Face *face = nullptr;
    U context = 0;
    int ppem = -1;
    U xscale = 0, italic = 0, matrix_set = 0, transform_result = ~0u;
    int matrix[4] = {};
    bool absolute = false;
    U spot_x = Spot, spot_y = Spot;
} current;

bool Select(const Font &font, const int *matrix, bool absolute = false, U spot_x = Spot, U spot_y = Spot)
{
    if (current.face != font.face)
    {
        if (current.context)
            Call(M::FreeContext, current.context);
        current = {};
        const U context = Call(M::NewContext, font.face->file);
        if (!context || context == ~0u)
            return false;
        current.face = font.face;
        current.context = context;
    }
    const bool same = current.ppem == font.ppem && current.xscale == font.xscale &&
                      current.italic == U(font.italic) && current.absolute == absolute &&
                      current.spot_x == spot_x && current.spot_y == spot_y &&
                      (matrix ? current.matrix_set && !memcmp(current.matrix, matrix, sizeof current.matrix)
                              : !current.matrix_set);
    if (same)
        return true;
    // {size, spot x, spot y, a, b, c, d}, 16.16. The em square
    // is ppem, simulated italic adds 57h/256 of it to c, the width
    // factor scales the high word of a and a MAT2 multiplies from
    // the left (the outline helper).
    int a = font.ppem << 16, b = 0, c = 0, d = font.ppem << 16;
    if (font.italic)
    {
        c += int(U(short((short(a >> 16) * 0x57) >> 8)) << 16);
        d += int(U(short((short(b >> 16) * 0x57) >> 8)) << 16);
    }
    if (font.xscale != 0x100)
        a = int(U(short((short(a >> 16) * int(font.xscale)) >> 8)) << 16);
    if (absolute)
    {
        a = matrix[0]; b = matrix[1]; c = matrix[2]; d = matrix[3];
    }
    else if (matrix)
    {
        auto fix = [](int x, int y) { return int((long long)x * y >> 16); };
        const int a0 = a, b0 = b, c0 = c, d0 = d;
        a = fix(matrix[0], a0) + fix(matrix[1], c0);
        b = fix(matrix[0], b0) + fix(matrix[1], d0);
        c = fix(matrix[2], a0) + fix(matrix[3], c0);
        d = fix(matrix[2], b0) + fix(matrix[3], d0);
    }
    const U record = M::Allocate(0x1c);
    const U fields[7] = {0x1c, spot_x, spot_y, U(a), U(b), U(c), U(d)};
    M::Write(record, fields, sizeof fields);
    const U result = Call(M::SetTransform, current.context, record);
    current.transform_result = result;
    M::Release(record);
    if (result == ~0u)
    {
        current.ppem = -1;
        return false;
    }
    current.absolute = absolute;
    current.spot_x = spot_x; current.spot_y = spot_y;
    current.ppem = font.ppem;
    current.xscale = font.xscale;
    current.italic = font.italic;
    current.matrix_set = matrix != nullptr;
    if (matrix)
        memcpy(current.matrix, matrix, sizeof current.matrix);
    return true;
}

// Entry 24 : flags, GLYPHMETRICS of 16.16 values and the image.
struct Metrics32
{
    int black_x, black_y, origin_x, origin_y, cell_x, cell_y;
};
U DriverGlyph(const Font &font, const int *matrix, U flags, U index, unsigned char *buffer, U size, Metrics32 &gm, bool keep_context = false)
{
    if (keep_context ? !current.context : !Select(font, matrix))
        return ~0u;
    const U metrics = M::Allocate(0x20), out = size ? M::Allocate(size) : 0;
    if (!metrics || (size && !out))
        return ~0u;
    const U result = Call(M::Glyph, current.context, flags, metrics, out, size, index);
    M::Read(metrics, &gm, sizeof gm);
    if (result != ~0u && buffer && size)
        M::Read(out, buffer, result < size ? result : size);
    M::Release(metrics);
    if (out)
        M::Release(out);
    return result;
}

int Round16(int value) { return (value + 0x8000) >> 16; }

// The name strings behind the header: slots of (offset, length) at 8Ah (ID 1),
// 8Eh (ID 4), 92h (ID 2) and 96h (ID 3). Windows names are
// UTF-16; Windows 98 stores them in Intel byte order.
void NameAt(const unsigned char *all, U total, U slot, char *out, U capacity)
{
    const U offset = UWord(all, slot), length = UWord(all, slot + 2);
    out[0] = 0;
    if (!offset || offset >= total || length > total - offset)
        return;
    U n = 0;
    for (U i = 0; i + 1 < length && n + 1 < capacity; i += 2)
    {
        const U c = all[offset + i] | U(all[offset + i + 1]) << 8;
        if (!c)
            break;
        out[n++] = char(c < 256 ? c : '?');
    }
    out[n] = 0;
}
} // namespace

namespace
{
// Glyph runs retain the DIB driver's WORD spacing and justification. Widths
// come from the selected outline's glyph metrics, not ANSI character slots.
unsigned MeasureGlyphs(const Font &font,const unsigned short *glyphs,unsigned count,
                       DibText::DrawMode &dm,const short *dx)
{
    int width=0,error=dm.break_error;
    const unsigned break_code=(font.view.data[0x5f]+font.view.data[0x62])&255;
    for(unsigned i=0;i<count;++i){
        GlyphImage image;if(!IndexGlyph(font,glyphs[i],image))return 0x80000000u;
        int extra=dm.char_extra;
        if(dm.total_break_extra&&glyphs[i]==font.index[break_code]){
            extra=short(extra+dm.break_extra);
            if(dm.break_remainder){
                error=short(error-dm.break_remainder);
                if(dm.break_remainder>=0?error<=0:error>=0){
                    error=short(error+(dm.break_remainder>=0?dm.break_count:-dm.break_count));
                    extra=short(extra+(dm.break_remainder>=0?1:-1));
                }
            }
        }
        width=short(width+(dx?dx[i]:image.advance)+extra);
    }
    dm.break_error=short(error);
    return (font.view.Height()<<16)|(unsigned(width)&65535);
}
unsigned OutlineText(DibEng::Bitmap *bitmap, int x, int y, const Win98::Rect &clip, const unsigned char *text,
                     int count, const DibText::Font &view, DibText::DrawMode &dm, const short *dx,
                     const Win98::Rect *opaque, unsigned options)
{
    const Font *font = FontOf(view);
    if(font&&(options&0x10)&&count<0)return MeasureGlyphs(*font,reinterpret_cast<const unsigned short*>(text),unsigned(-count),dm,dx);
    return font && bitmap && Draw(*bitmap, x, y, clip, text, count, *font, dm, dx, opaque, options) ? 0 : 0x80000000u;
}
} // namespace

bool Start(Reader reader, Query query)
{
    M::SetReader(reader);
    M::SetQuery(query);
    DibText::SetOutlineText(OutlineText);
    return M::Start();
}
bool Available() { return M::Started(); }
const char *Failure() { return M::Failure(); }
U FaceCount() { return face_count; }
const Face *FaceAt(U i) { return i < face_count ? faces + i : nullptr; }

const Face *Install(const char *name)
{
    if (!M::Started() || face_count == FaceCapacity)
        return nullptr;
    const char *leaf = name;
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == ':' || *p == '/')
            leaf = p + 1;
    for (U i = 0; i < face_count; ++i)
    {
        const char *a = faces[i].leaf, *b = leaf;
        while (*a && (*a | 32) == (*b | 32))
            ++a, ++b;
        if (!*a && !*b)
            return faces + i;
    }
    char path[260];
    unsigned n = 0;
    const char *prefix = leaf == name ? "C:\\WINDOWS\\FONTS\\" : "";
    for (const char *p = prefix; *p && n + 1 < sizeof path; ++p)
        path[n++] = *p;
    for (const char *p = name; *p && n + 1 < sizeof path; ++p)
        path[n++] = *p;
    path[n] = 0;
    // Entry 34 opens and maps the file.
    const U text = M::String(path);
    const U file = text ? Call(M::OpenFile, text, 0) : ~0u;
    if (text)
        M::Release(text);
    if (!file || file == ~0u)
        return nullptr;
    Face &f = faces[face_count];
    f = {};
    f.file = file;
    for (U i = 0; i + 1 < sizeof f.leaf && leaf[i]; ++i)
        f.leaf[i] = leaf[i];
    // Entry 31 : the header, then header and strings together.
    // The total is the header plus every string length.
    const U scratch = M::Allocate(0x400);
    const U got = Call(M::FileHeader, file, 0, scratch, 0x9a);
    if (got != 0x9a)
    {
        M::Release(scratch);
        Call(M::CloseFile, file);
        return nullptr;
    }
    M::Read(scratch, f.header, 0x9a);
    const unsigned char *h = f.header;
    const U total = 0x9a + UWord(h, 0x8c) + UWord(h, 0x90) + UWord(h, 0x94) + UWord(h, 0x98);
    unsigned char all[0x400] = {};
    if (total <= sizeof all && Call(M::FileHeader, file, 0, scratch, total) == total)
    {
        M::Read(scratch, all, total);
        NameAt(all, total, 0x8a, f.family, sizeof f.family);
        NameAt(all, total, 0x92, f.style, sizeof f.style);
        NameAt(all, total, 0x8e, f.full, sizeof f.full);
    }
    M::Release(scratch);
    if (!f.family[0])
    {
        Call(M::CloseFile, file);
        return nullptr;
    }
    // the directory entry. Ascent is usWinAscent (header 2),
    // the cell adds usWinDescent (header 4 holds its negative), the internal
    // leading is the cell less the em, the external leading what hhea's line
    // spacing adds to the cell (never negative).
    f.upem = Word(h, 0x24);
    f.ascent = Word(h, 2);
    f.cell = short(f.ascent - Word(h, 4));
    f.internal = short(f.cell - f.upem);
    const int spacing = short(Word(h, 0x3c) + Word(h, 0x38) - Word(h, 0x3a) - f.cell);
    f.external = spacing < 0 ? 0 : spacing;
    const U selection = UWord(h, 0x1a);
    f.italic = selection & 1;
    f.underline = selection & 2;
    f.strikeout = selection & 0x10;
    // usWeightClass 1..9 is the old scale in hundreds (table ).
    f.weight = UWord(h, 6);
    if (f.weight >= 1 && f.weight <= 9)
        f.weight *= 100;
    f.charset = h[0x5a];
    // the family from PANOSE bFamilyType, bSerifStyle (table
    // ) and bProportion; bit 0 is set for a proportional font, bits 1
    // and 2 are TMPF_VECTOR and TMPF_TRUETYPE.
    static const unsigned char serif[16] = {0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                                            0x10, 0x10, 0x10, 0x20, 0x20, 0x20, 0x20, 0x20};
    const U kind = h[0x10], proportion = h[0x13];
    U family = 0x30;
    if (kind == 3)
        family = 0x40;
    else if (kind == 4)
        family = 0x50;
    else if (proportion != 9)
        family = serif[h[0x11] & 15];
    f.pitch = (family + (proportion != 9 ? 1 : 0)) | 6;
    f.average = Word(h, 0xa) ? Word(h, 0xa) : int(UWord(h, 8) >> 1);
    f.maximum = short(Word(h, 0x34) - Word(h, 0x30));
    // the character range. A symbol font (both ends above FFh)
    // starts at 20h with its codes at usFirstCharIndex - 20h.
    U first = UWord(h, 0xc), last = UWord(h, 0xe);
    if (last > 0xff)
    {
        if (first > 0xff)
        {
            f.symbol_base = first - 0x20;
            last = last - first < 0xdf ? last - first + 0x20 : 0xff;
            first = 0x20;
        }
        else
            last = 0xff;
    }
    if (first < 0x20)
        first = 0x20;
    f.first = first - 1;
    f.last = last;
    f.glyphs = UWord(h, 0x58);
    f.type = 3;
    ++face_count;
    return &f;
}

const Font *Realize(const Face *face, int height, int width, U weight, bool italic, bool underline, bool strikeout,
                    bool describe, int escapement)
{
    if (!face)
        return nullptr;
    // A height of zero is GDI's default of twelve points.
    if (!height)
        height = -MulDiv16(12, LogPixels, 72);
    // GDI's TrueType mapper replaces requests from -2 to
    // +2 with a two-pixel em, before converting a positive cell height.
    if (height >= -2 && height <= 2)
        height = -2;
    // bold is simulated when the request is more than 150 above
    // the face; italic when the request is italic and the face is not.
    const bool bold = weight && int(face->weight) + 150 < int(weight);
    const bool slant = italic && !face->italic;
    for (U i = 0; i < font_count && !describe; ++i)
    {
        const Font &f = *fonts[i];
        if (f.face == face && f.height == height && f.width == width && f.bold == bold && f.italic == slant &&
            f.underline == underline && f.strikeout == strikeout && f.escapement == escapement)
            return &f;
    }
    if (font_count == FontCapacity)
        return nullptr;
    const unsigned char *h = face->header;
    // a negative height is the em in pixels. A positive one is
    // the cell: the VDMX answer (entry 23 ) or a search from the
    // proportional guess, one pixel at a time, until the cell height matches
    // or the search turns.
    int ppem;
    if (height < 0)
        ppem = -height;
    else
    {
        U vdmx = Call(M::Vdmx, face->file, U(LogPixels), U(LogPixels), U(height & 0xffff));
        if (vdmx == ~0u)
            vdmx = 0;
        if (vdmx & 0xffff)
            ppem = short(vdmx & 0xffff);
        else
        {
            int guess = MulDiv16(height, face->upem, face->cell), direction = 0;
            for (;;)
            {
                U probe = Call(M::Vdmx, face->file, U(LogPixels), U(LogPixels), U(-guess & 0xffff));
                if (probe == ~0u)
                    probe = 0;
                int cell = short(probe & 0xffff) + short(probe >> 16);
                if (!(cell & 0xffff))
                    cell = MulDiv16(guess, face->ascent, face->upem) +
                           MulDiv16(guess, face->cell - face->ascent, face->upem);
                if ((cell & 0xffff) == U(height & 0xffff))
                    break;
                if (U(cell & 0xffff) > U(height & 0xffff))
                {
                    --guess;
                    if (direction & 2)
                        break;
                    direction = 1;
                }
                else
                {
                    if (direction & 1)
                        break;
                    ++guess;
                    direction = 2;
                }
            }
            ppem = guess;
        }
    }
    if (ppem <= 0)
        ppem = 1;
    // lfWidth against the average width at this ppem; a bold
    // simulation takes one pixel off the request first.
    U xscale = 0x100;
    if (width)
    {
        const int units = MulDiv16(face->upem, LogPixels, LogPixels);
        const int average = MulDiv16(face->average, ppem, units);
        int wanted = width < 0 ? -width : width;
        if (average)
        {
            if (bold)
                --wanted;
            const unsigned product = 0x100u * unsigned(wanted & 0xffff);
            if ((product >> 16) < unsigned(average))
            {
                const unsigned factor = (product + unsigned(average >> 1)) / unsigned(average);
                if (factor & 0xffff)
                    xscale = factor & 0xffff;
            }
        }
    }
    Font *font = new Font;
    font->face = face;
    font->height = height;
    font->escapement = escapement;
    font->width = width;
    font->ppem = ppem;
    font->xscale = xscale;
    font->bold = bold;
    font->italic = slant;
    font->underline = underline;
    font->strikeout = strikeout;
    // the realized metrics.
    const int xunits = MulDiv16(face->upem, LogPixels, int((unsigned(LogPixels) * xscale) >> 8));
    int ascent = MulDiv16(face->ascent, ppem, face->upem);
    const int external = MulDiv16(face->external, ppem, face->upem);
    const int descent = MulDiv16(face->cell - face->ascent, ppem, face->upem);
    int cell = ascent + descent;
    int average = width ? (width < 0 ? -width : width) : MulDiv16(face->average, ppem, xunits);
    int maximum = face->maximum;
    if (slant)
        maximum += int((unsigned(face->cell) * 0x57u * xscale) >> 16);
    maximum = MulDiv16(maximum, ppem, xunits);
    // without escapement and simulated italic, the VDMX record of
    // the ppem sets ascent and cell.
    if (!slant)
    {
        U vdmx = Call(M::Vdmx, face->file, (U(LogPixels) * xscale) >> 8, U(LogPixels), U(-ppem & 0xffff));
        if (vdmx == ~0u)
            vdmx = 0;
        const int below = short(vdmx & 0xffff), above = short(vdmx >> 16);
        if (short(above + below))
        {
            ascent = above;
            cell = short(above + below);
        }
    }
    // Underline and strikeout from post and
    // OS/2, as rows below the top of the cell.
    int underline_position = MulDiv16(Word(h, 0x54), ppem, face->upem);
    int underline_thickness;
    if (Word(h, 0x56))
        underline_thickness = MulDiv16(Word(h, 0x56), ppem, face->upem);
    else
    {
        const int d = short(Word(h, 4) - Word(h, 2)) >> 4;
        underline_position = MulDiv16(2 * d, ppem, face->upem);
        underline_thickness = MulDiv16(d, ppem, face->upem);
    }
    if (!underline_thickness)
        underline_thickness = 1;
    int strike_position = MulDiv16(Word(h, 0x52), ppem, face->upem);
    int strike_thickness;
    if (Word(h, 0x50))
        strike_thickness = MulDiv16(Word(h, 0x50), ppem, face->upem);
    else
    {
        const int d = int(U(Word(h, 2) - Word(h, 4)) & 0xffff) >> 4;
        const int typo = int(UWord(h, 0x26) >> 1);
        strike_position = MulDiv16(typo + (typo >> 1), ppem, face->upem);
        strike_thickness = MulDiv16(d, ppem, face->upem);
    }
    if (!strike_thickness)
        strike_thickness = 1;
    font->ascent = ascent;
    font->descent = cell - ascent;
    font->internal = cell - ppem;
    font->external = external;
    font->average = average + (bold ? 1 : 0);
    font->maximum = maximum + (bold ? 1 : 0);
    font->underline_top = ascent - underline_position;
    font->underline_thickness = underline_thickness;
    font->strike_top = ascent - strike_position;
    font->strike_thickness = strike_thickness;
    font->weight = face->weight + (bold ? 300 : 0);   //
    // the glyph index of each code. A text font goes through
    // GDI's ANSI table into Unicode, a symbol font takes the codes
    // from its own start (FONTINFO+50h) upwards; entry 30 looks them up.
    if (!describe)
    {
        const U out = M::Allocate(0x200), table = M::Allocate(0x200);
        U result = ~0u;
        if (out && table)
        {
            if (face->symbol_base)
                result = Call(M::GlyphIndices, face->file, out, 0x200, 0, face->symbol_base);
            else
            {
                M::Write(table, OriginalGdiAnsiCharset, 0x200);
                result = Call(M::GlyphIndices, face->file, out, 0x200, table, 0x0100ff1fu);
            }
        }
        if (result != ~0u)
            M::Read(out, font->index, sizeof font->index);
        if (out)
            M::Release(out);
        if (table)
            M::Release(table);
    }
    // the advance of every glyph index through entry 25 in
    // mode 4, as Windows 98 builds GDI's width table; Windows 98 adds the pixel of a
    // bold simulation to every character.
    unsigned short *advances = nullptr;
    if (!describe && face->glyphs && Select(*font, nullptr))
    {
        const U bytes = face->glyphs * 2, out = M::Allocate(bytes);
        if (out && Call(M::Widths, current.context, 0x80000004u, out, bytes, 0, 0) != ~0u)
        {
            advances = static_cast<unsigned short *>(calloc(face->glyphs, 2));
            if (advances)
                M::Read(out, advances, bytes);
        }
        if (out)
            M::Release(out);
    }
    // The header the DIB engine port measures with: FNT 3.0, codes 0..255, one
    // blank bitmap every entry points at.
    const int widest = maximum + 1 > 1 ? maximum + 1 : 1;
    const U rows = U(cell > 0 ? cell : 1), blank_bytes = U((widest + 7) / 8) * rows;
    const U table = 148, count = 256, blank = table + (count + 1) * 6;
    const char *name = face->family;
    const U face_at = blank + blank_bytes, length = face_at + U(strlen(name)) + 1;
    unsigned char *f = static_cast<unsigned char *>(calloc(length, 1));
    if (!f)
    {
        free(advances);
        delete font;
        return nullptr;
    }
    Put16(f, 0, 0x300);
    Put32(f, 2, length);
    Put16(f, 0x44, U(72 * ppem / LogPixels));       // dfPoints,
    Put16(f, 0x46, LogPixels);
    Put16(f, 0x48, LogPixels);
    Put16(f, 0x4a, U(ascent));
    Put16(f, 0x4c, U(cell - ppem));
    Put16(f, 0x4e, U(external));
    f[0x50] = (face->italic || slant) ? 0xff : 0;
    f[0x51] = underline ? 1 : 0;
    f[0x52] = strikeout ? 1 : 0;
    Put16(f, 0x53, font->weight);
    f[0x55] = U8(face->charset);
    Put16(f, 0x56, 0);
    Put16(f, 0x58, rows);
    f[0x5a] = U8(face->pitch);
    Put16(f, 0x5b, U(font->average));
    Put16(f, 0x5d, U(font->maximum));
    f[0x5f] = 0;
    f[0x60] = 0xff;
    f[0x61] = 0x1f;                                  // default: GDI's 1Fh maps to U+0000
    f[0x62] = 0x20;                                  // break: space
    Put16(f, 0x63, U((widest + 7) / 8));
    Put32(f, 0x69, face_at);
    Put32(f, 0x71, blank);
    f[0x84] = 'T';
    f[0x85] = 'T';
    Put16(f, 0x86, U(font->underline_top));
    Put16(f, 0x88, U(underline_thickness));
    Put16(f, 0x8a, U(font->strike_top));
    Put16(f, 0x8c, U(strike_thickness));
    f[0x8e] = bold ? 1 : 0;
    f[0x90] = 0x1f;                                  // tmFirstChar..tmBreakChar
    f[0x91] = 0xff;
    f[0x92] = 0x1f;
    f[0x93] = 0x20;
    for (U code = 0; code <= count; ++code)
    {
        U advance = 0;
        if (code < count && advances && font->index[code] < face->glyphs)
            advance = advances[font->index[code]] + (bold ? 1 : 0);
        else if (describe && code < count)
            advance = U(font->average);
        Put16(f, table + code * 6, advance > 0xffff ? 0xffff : advance);
        Put32(f, table + code * 6 + 2, blank);
    }
    memcpy(f + face_at, name, strlen(name) + 1);
    free(advances);
    font->fnt = f;
    font->fnt_length = length;
    font->view.data = f;
    font->view.length = length;
    font->view.outline = font;
    if (!describe)
        fonts[font_count++] = font;
    return font;
}

namespace
{
// one glyph by index, format GGO_BITMAP with 30h and the index
// bit, the buffer grown until the image fits.
void Fetch(const Font &font, U index, Glyph &g)
{
    g.ready = true;
    Metrics32 gm{};
    U size = 0x100;
    U bytes = ~0u;
    unsigned char *buffer = nullptr;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        free(buffer);
        buffer = static_cast<unsigned char *>(calloc(size, 1));
        if (!buffer)
            break;
        bytes = DriverGlyph(font, nullptr, 0x80000031u, index, buffer, size, gm);
        if (bytes == ~0u || bytes <= size)
            break;
        size = bytes + 0xff;
    }
    if (bytes != ~0u && buffer)
    {
        GlyphImage &im = g.image;
        // An empty glyph (the space) answers no bytes and a box of one pixel;
        // it draws nothing.
        im.width = bytes ? gm.black_x : 0;
        im.height = bytes ? gm.black_y : 0;
        im.stride = ((gm.black_x + 31) / 32) * 4;
        im.origin_x = Round16(gm.origin_x);
        im.origin_y = Round16(gm.origin_y);
        im.advance = Round16(gm.cell_x);
        if (font.bold && im.width)
        {
            // Simulated bold is one pixel wider: every row
            // ORed with itself one pixel to the right.
            const int wide = im.width + 1, stride = ((wide + 31) / 32) * 4;
            unsigned char *bold = static_cast<unsigned char *>(calloc(U(stride * im.height) + 1, 1));
            if (bold)
            {
                for (int y = 0; y < im.height; ++y)
                    for (int x = 0; x < im.width; ++x)
                        if (buffer[y * im.stride + x / 8] & (0x80 >> (x & 7)))
                            for (int k = 0; k < 2; ++k)
                                bold[y * stride + (x + k) / 8] |= U8(0x80 >> ((x + k) & 7));
                free(buffer);
                buffer = bold;
                im.width = wide;
                im.stride = stride;
            }
            ++im.advance;
        }
        g.storage = buffer;
        buffer = nullptr;
        im.bits = g.storage;
        g.ok = true;
    }
    free(buffer);
}
} // namespace

bool TextGlyph(const Font &font, unsigned char code, GlyphImage &image)
{
    Font &f = const_cast<Font &>(font);
    Glyph *&g = f.glyphs[code];
    if (!g)
        g = new Glyph;
    if (!g->ready)
        Fetch(font, font.index[code], *g);
    image = g->image;
    return g->ok;
}

U UnicodeIndex(const Font &font, U unicode)
{
    // Entry 30 with a table of one code, as Windows 98 passes GDI's charset
    // tables.
    const U out = M::Allocate(4), table = M::Allocate(4);
    U index = 0;
    if (out && table)
    {
        const unsigned short code = (unsigned short)unicode;
        M::Write(table, &code, 2);
        if (Call(M::GlyphIndices, font.face->file, out, 2, table, 0x0100ff1fu) != ~0u)
        {
            unsigned short value = 0;
            M::Read(out, &value, 2);
            index = value;
        }
    }
    if (out)
        M::Release(out);
    if (table)
        M::Release(table);
    return index;
}

bool IndexGlyph(const Font &font, U index, GlyphImage &image)
{
    Font &f = const_cast<Font &>(font);
    if (index >= f.face->glyphs)
        return false;
    if (!f.by_index)
    {
        f.by_index = static_cast<Glyph **>(calloc(f.face->glyphs, sizeof(Glyph *)));
        if (!f.by_index)
            return false;
    }
    Glyph *&g = f.by_index[index];
    if (!g)
        g = new Glyph;
    if (!g->ready)
        Fetch(font, index, *g);
    image = g->image;
    return g->ok;
}

U EngineContext(const Font &font, int angle, int dpi_x, int dpi_y, U spot_x, U spot_y)
{
    // Windows 98 builds integer high words, including the original
    // trigonometric table and the printer's asymmetric resolution.
    int a = Win98EngineFont::Sine(font.ppem, angle + 900);
    int b = Win98EngineFont::Sine(font.ppem, angle), c = -b, d = a;
    if (font.italic) { c = short(c + ((short(a) * 0x57) >> 8)); d = short(d + ((short(b) * 0x57) >> 8)); }
    if (font.xscale != 0x100) { a = short((short(a) * int(font.xscale)) >> 8); b = short((short(b) * int(font.xscale)) >> 8); }
    if (dpi_x != dpi_y && dpi_y) { a = short((a * dpi_x + dpi_y / 2) / dpi_y); c = short((c * dpi_x + dpi_y / 2) / dpi_y); }
    const int transform[4] = {int(U(a) << 16), int(U(b) << 16), int(U(c) << 16), int(U(d) << 16)};
    return Select(font, transform, true, spot_x, spot_y) ? current.transform_result : ~0u;
}
U EngineGlyph(const Font &font, U character, bool index, U format, unsigned char *buffer, U size, int (&metrics)[6])
{
    const U glyph = index ? character : font.index[character & 0xff];
    const U flags = (format & ~4u) | ((format & 8u) >> 1) | 0x80000030u;
    Metrics32 gm{};
    const U result = DriverGlyph(font, nullptr, flags, glyph, buffer, size, gm, true);
    memcpy(metrics, &gm, sizeof gm);
    return result;
}

U GlyphOutline(const Font &font, U character, U format, const int *matrix, unsigned char *buffer, U size,
               GlyphMetrics &out)
{
    // the format is the low three bits; 3 fails.
    const U kind = format & 7;
    if (kind == 3 || kind > 6)
        return ~0u;
    U index = character & 0xffff;
    if (!(format & 0x80))
    {
        if (index >= 0x100)
            return ~0u;
        index = font.index[index];
    }
    // an identity MAT2 counts as none.
    const int identity[4] = {0x10000, 0, 0, 0x10000};
    int rotated[4];
    if (font.escapement)
    {
        // The driver's transform uses y-up glyph coordinates. Compose the
        // caller's MAT2 with the font direction before requesting the bitmap
        // or metrics; rotating only TextOut leaves GetGlyphOutline wrong.
        const int *m=matrix?matrix:identity;
        int rotation[4]={1,0,0,1};
        switch ((font.escapement/900%4+4)%4) {
        case 1: rotation[0]=rotation[3]=0;rotation[1]=1;rotation[2]=-1;break;
        case 2: rotation[0]=rotation[3]=-1;break;
        case 3: rotation[0]=rotation[3]=0;rotation[1]=-1;rotation[2]=1;break;
        }
        rotated[0]=int(U(m[0])*U(rotation[0])+U(m[1])*U(rotation[2]));
        rotated[1]=int(U(m[0])*U(rotation[1])+U(m[1])*U(rotation[3]));
        rotated[2]=int(U(m[2])*U(rotation[0])+U(m[3])*U(rotation[2]));
        rotated[3]=int(U(m[2])*U(rotation[1])+U(m[3])*U(rotation[3]));
        matrix=rotated;
    }
    if (matrix && !memcmp(matrix, identity, sizeof identity))
        matrix = nullptr;
    Metrics32 gm{};
    const U flags = 0x80000000u | 0x30u | kind;
    const U bytes = DriverGlyph(font, matrix, flags, index, buffer, buffer ? size : 0, gm);
    if (bytes == ~0u)
        return ~0u;
    // the box as it is, origin and increments rounded.
    out.black_x = U(gm.black_x);
    out.black_y = U(gm.black_y);
    out.origin_x = short(Round16(gm.origin_x));
    out.origin_y = short(Round16(gm.origin_y));
    out.cell_x = short(Round16(gm.cell_x));
    out.cell_y = short(Round16(gm.cell_y));
    return bytes;
}

bool AbcWidths(const Font &font, U first, U last, int *abc)
{
    // Metrics only (format 0 with 30h and the
    // index bit); A is the rounded origin and C the rest of the rounded
    // advance, both kept as signed bytes, B what the advance leaves between
    // them. Unlike GetCharWidth the ABC widths do
    // not carry the pixel of a simulated bold.
    for (U code = first; code <= last; ++code)
    {
        Metrics32 gm{};
        if (DriverGlyph(font, nullptr, 0x80000030u, font.index[code & 0xff], nullptr, 0, gm) == ~0u)
            return false;
        const int advance = short(Round16(gm.cell_x));
        const int a = (signed char)Round16(gm.origin_x);
        const int c = (signed char)(advance - a - gm.black_x);
        abc[(code - first) * 3] = a;
        abc[(code - first) * 3 + 1] = advance - a - c;
        abc[(code - first) * 3 + 2] = c;
    }
    return true;
}

U FontData(const Font &font, U table, U offset, unsigned char *buffer, U size)
{
    const U out = size && buffer ? M::Allocate(size) : 0;
    if (size && buffer && !out)
        return ~0u;
    const U result = Call(M::FontData, font.face->file, table, offset, out, buffer ? size : 0);
    if (result != ~0u && out)
        M::Read(out, buffer, result < size ? result : size);
    if (out)
        M::Release(out);
    return result;
}

U KerningPairs(const Font &font, U count, unsigned char *pairs)
{
    // the charset table of the realized charset, the
    // transformation, then entry 25 in mode 3 with room for `count` records of
    // first, second and amount (six bytes each). A charset without a table
    // (a symbol font) has no pairs.
    if (font.face->symbol_base || font.face->charset != 0 || !Select(font, nullptr))
        return 0;
    const U bytes = count * 6;
    const U table = M::Allocate(0x200), out = bytes && pairs ? M::Allocate(bytes) : 0;
    if (!table || (bytes && pairs && !out))
        return 0;
    M::Write(table, OriginalGdiAnsiCharset, 0x200);
    U result = Call(M::Widths, current.context, 3, out, bytes, table, 0);
    if (result == ~0u)
        result = 0;
    if (out && result)
        M::Read(out, pairs, (result < count ? result : count) * 6);
    M::Release(table);
    if (out)
        M::Release(out);
    return result;
}

U OutlineTextMetrics(const Font &font, const Gdi::TEXTMETRICA &tm, unsigned char *buffer, U size)
{
    const Face &face = *font.face;
    // the header and the four names behind it; makes
    // each UTF-16 name an ANSI string with its terminator.
    const unsigned char *h = face.header;
    const U units = UWord(h, 0x8c) + UWord(h, 0x90) + UWord(h, 0x94) + UWord(h, 0x98);
    const U size16 = (units >> 1) + 0x72;   //
    // GDI32 adds 62h, the growth from the Win16 to the Win32 layout.
    if (!buffer)
        return size16 + 0x62;
    unsigned char all[0x400] = {};
    const U total = 0x9a + units;
    const U scratch = M::Allocate(0x400);
    if (!scratch || total > sizeof all || Call(M::FileHeader, face.file, 0, scratch, total) != total)
    {
        if (scratch)
            M::Release(scratch);
        return 0;
    }
    M::Read(scratch, all, total);
    M::Release(scratch);
    unsigned char out[0x400] = {};
    auto put32 = [&](U at, U v) { Put32(out, at, v); };
    // otmSize ( rounds the Win16 size up to even) and
    // the TEXTMETRIC of the font.
    put32(0, ((size16 + 1) & ~1u) + 0x62);
    const int words[11] = {tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmInternalLeading, tm.tmExternalLeading,
                           tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmWeight, tm.tmOverhang,
                           tm.tmDigitizedAspectX, tm.tmDigitizedAspectY};
    for (U i = 0; i < 11; ++i)
        put32(4 + i * 4, U(words[i]));
    const unsigned char bytes[9] = {tm.tmFirstChar, tm.tmLastChar, tm.tmDefaultChar, tm.tmBreakChar, tm.tmItalic,
                                    tm.tmUnderlined, tm.tmStruckOut, tm.tmPitchAndFamily, tm.tmCharSet};
    memcpy(out + 48, bytes, sizeof bytes);
    // PANOSE, fsSelection, fsType, caret slope, italic angle and the em as
    // the header has them.
    memcpy(out + 61, h + 0x10, 10);
    put32(72, UWord(h, 0x1a));
    put32(76, UWord(h, 0x1c));
    put32(80, UWord(h, 0x1e));
    put32(84, UWord(h, 0x20));
    put32(88, U(Word(h, 0x22)));
    put32(92, UWord(h, 0x24));
    // each value times ppem over the em, the horizontal ones
    // (mask bits clear) first times the width factor in 1/256.
    auto scale = [&](U at, bool horizontal) {
        int v = Word(h, at);
        if (horizontal)
            v = short((v * int(font.xscale)) >> 8);
        return MulDiv16(v, font.ppem, face.upem);
    };
    static const unsigned char first[12] = {0x26, 0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x32, 0x34, 0x36, 0x38, 0x3a, 0x3c};
    static const bool first_x[12] = {false, false, false, false, false, true, false, true, false, false, false, false};
    // Ascent unsigned, Descent and LineGap signed, cap and x
    // height unsigned, the font box signed, Mac ascent and descent signed,
    // Mac line gap unsigned.
    static const bool first_signed[12] = {false, true, true, false, false, true, true, true, true, true, true, false};
    for (U i = 0; i < 12; ++i)
    {
        const int v = scale(first[i], first_x[i]);
        put32(96 + i * 4, first_signed[i] ? U(short(v)) : U(v & 0xffff));
    }
    put32(144, UWord(h, 0x3e));             // otmusMinimumPPEM, not scaled
    // Sub- and superscript, strikeout, and
    // the underscore, whose two Win16 words GDI32 takes in swapped order.
    for (U i = 0; i < 8; ++i)
        put32(148 + i * 4, U(short(scale(0x40 + i * 2, !(i & 1)))));
    put32(180, U(scale(0x50, false) & 0xffff));
    put32(184, U(short(scale(0x52, false))));
    put32(188, U(scale(0x56, false) & 0xffff));
    put32(192, U(short(scale(0x54, false))));
    // The names: family (ID 1), face (ID 4), style (ID 2) and full (ID 3),
    // in this order behind the structure.
    static const unsigned char slots[4] = {0x8a, 0x8e, 0x92, 0x96};
    U at = 0xd4;
    for (U i = 0; i < 4; ++i)
    {
        put32(196 + i * 4, at);
        char name[128];
        NameAt(all, total, slots[i], name, sizeof name);
        const U n = U(strlen(name)) + 1;
        if (at + n > sizeof out)
            return 0;
        memcpy(out + at, name, n);
        at += n;
    }
    // cbData bytes go to the caller.
    memcpy(buffer, out, size < sizeof out ? size : sizeof out);
    return 1;
}

bool Draw(DibEng::Bitmap &bitmap, int x, int y, const Win98::Rect &clip, const unsigned char *text, int count,
          const Font &font, DibText::DrawMode &dm, const short *dx, const Win98::Rect *opaque, U options)
{
    const DibText::Font &view = font.view;
    if (!DibEng::Valid(bitmap) || (bitmap.depth != 32 && bitmap.depth != 24 && bitmap.depth != 16 && bitmap.depth != 8 && bitmap.depth != 1) ||
        count < 0 || count > 4096)
        return false;
    Win98::Rect bounded;
    const Win98::Rect screen = {0, 0, bitmap.width, bitmap.height};
    if (!Win98::IntersectRect(&bounded, &clip, &screen))
        return true;
    // DIB_EXTTEXTOUT: clip and opaque rectangle.
    if ((options & 4) && opaque && !Win98::IntersectRect(&bounded, &bounded, opaque))
        return true;
    if ((options & 2) && opaque)
    {
        Win98::Rect fill;
        if (Win98::IntersectRect(&fill, &bounded, opaque))
            DibEng::BitBlt(bitmap, fill.left, fill.top, fill.right - fill.left, fill.bottom - fill.top, nullptr, 0, 0,
                           0xf00021, {dm.background});
    }
    if (!count)
        return true;
    // Advance the pen as in the measuring pass of DibText::ExtTextOut,
    // using the glyph advance or lpDx, character extra and justification.
    DibText::DrawMode measuring = dm;
    const unsigned extent = DibText::ExtTextOut(nullptr, 0, 0, {}, text, -count, view, measuring, dx,nullptr,options&0x10);
    if (extent == 0x80000000u)
        return false;
    const int height = int(view.Height());
    // An opaque run fills its cell with the background first; the glyphs are
    // then drawn in the foreground over it and over what lies beside it.
    if (dm.background_mode == 2)
    {
        auto cell = RotateRect(font.escapement, {0, 0, int(extent & 0xffff), height});
        cell.left+=x;cell.right+=x;cell.top+=y;cell.bottom+=y;
        Win98::Rect fill;
        if (Win98::IntersectRect(&fill, &bounded, &cell))
            DibEng::BitBlt(bitmap, fill.left, fill.top, fill.right - fill.left, fill.bottom - fill.top, nullptr, 0, 0,
                           0xf00021, {dm.background});
    }
    const bool justify = dm.total_break_extra != 0, distribute = justify && dm.break_remainder != 0;
    int error = dm.break_error;
    const int baseline = y + font.ascent;
    for (int i = 0, pen = x; i < count; ++i)
    {
        const bool indexed=(options&0x10)!=0;
        const unsigned char code = indexed?0:text[i];
        const unsigned glyph=indexed?reinterpret_cast<const unsigned short*>(text)[i]:font.index[code];
        GlyphImage g;
        if(!(indexed?IndexGlyph(font,glyph,g):TextGlyph(font,code,g)))return false;
        if (g.width && g.height)
        {
            const int left = pen + g.origin_x, top = baseline - g.origin_y;
            for (int row = 0; row < g.height; ++row)
            {
                const int py = top + row;
                // GDI clips text vertically to the realized ascent/descent
                // (including VDMX). Horizontal glyph overhangs remain visible;
                // GetGlyphOutline still exposes the full driver bitmap.
                if ((!font.escapement && (py < bounded.top || py >= bounded.bottom)) || py < y || py >= y + height)
                    continue;
                const unsigned char *line = g.bits + row * g.stride;
                for (int col = 0; col < g.width; ++col)
                {
                    if (!font.escapement)
                    {
                        const int px=left+col;
                        if (px>=bounded.left && px<bounded.right && (line[col>>3]&(0x80>>(col&7))))
                            DibEng::WritePixel(bitmap,px,py,dm.foreground);
                        continue;
                    }
                    int px=left+col-x, dest_y=py-y;
                    // Rotate pixel centers, retaining integer cell boundaries.
                    auto pixel=RotateRect(font.escapement,{px,dest_y,px+1,dest_y+1});
                    px=x+pixel.left;dest_y=y+pixel.top;
                    if (px >= bounded.left && px < bounded.right && dest_y >= bounded.top && dest_y < bounded.bottom &&
                        (line[col >> 3] & (0x80 >> (col & 7))))
                        DibEng::WritePixel(bitmap, px, dest_y, dm.foreground);
                }
            }
        }
        // character extra, then the justification of a break.
        const unsigned width = indexed?unsigned(g.advance):view.Width(view.Index(code));
        int extra = dm.char_extra;
        if ((indexed?glyph==font.index[(view.data[0x5f]+view.data[0x62])&255]:view.Index(code)==view.data[0x62]) && justify)
        {
            extra = short(extra + dm.break_extra);
            if (distribute)
            {
                error = short(error - dm.break_remainder);
                if (dm.break_remainder >= 0 ? error <= 0 : error >= 0)
                {
                    error = short(error + (dm.break_remainder >= 0 ? dm.break_count : -dm.break_count));
                    extra = short(extra + (dm.break_remainder >= 0 ? 1 : -1));
                }
            }
        }
        pen = short(pen + (dx ? dx[i] : int(width)) + extra);
    }
    dm.break_error = short(error);
    return true;
}
} // namespace GdiTrueType
