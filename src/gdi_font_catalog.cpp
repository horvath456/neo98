// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi_font_catalog.h"
#include <stdlib.h>
#include <string.h>
#include "gdi_truetype.h"
#include "gdi_display_dpi.h"
namespace GdiFonts
{
namespace
{
constexpr unsigned Capacity = 512, SubstituteCapacity = 32;
Face faces[Capacity];
unsigned char *owned[Capacity];
unsigned count = 0;
int roles[RoleCount] = {-1, -1, -1};
struct Pair { char from[32], to[32]; } substitutes[SubstituteCapacity];
unsigned substitute_count = 0;

unsigned Word(const unsigned char *p) { return p[0] | unsigned(p[1]) << 8; }
unsigned Dword(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }
char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }
void Copy(char *out, const char *in, unsigned size)
{
    unsigned i = 0;
    for (; i + 1 < size && in[i]; ++i) out[i] = in[i];
    out[i] = 0;
}
bool Add(const char *file, const unsigned char *fnt, unsigned size, bool copy, unsigned type, unsigned ppem)
{
    if (count == Capacity || !fnt || size < 118)
        return false;
    // dfSize bounds the image; a resource slot is rounded up to its alignment.
    const unsigned length = Dword(fnt + 2) && Dword(fnt + 2) <= size ? Dword(fnt + 2) : size;
    unsigned char *data = nullptr;
    if (copy)
    {
        data = static_cast<unsigned char *>(malloc(length));
        if (!data)
            return false;
        memcpy(data, fnt, length);
    }
    Face face;
    if (!face.font.Load(copy ? data : fnt, length) || !face.font.FaceName(face.name))
    {
        free(data);
        return false;
    }
    face.type = type;
    face.ppem = ppem;
    Copy(face.file, file, sizeof face.file);
    owned[count] = data;
    faces[count++] = face;
    return true;
}
int Distance(int a, int b) { return a > b ? a - b : b - a; }
// GDI enlarges a raster font up to eight times.
constexpr unsigned MaximumScale = 8;
void Put16(unsigned char *p, unsigned value) { p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); }
void Put32(unsigned char *p, unsigned value) { Put16(p, value); Put16(p + 2, value >> 16); }
} // namespace

// The image is written as a version 3.0 font, whose character table has
// 32-bit offsets: the 118 bytes of the header with the sizes multiplied, the
// table, the face name and the glyphs. A glyph is stored in columns of eight
// pixels, each column from top to bottom.
unsigned char *ScaledFnt(const Face &face, unsigned scale, unsigned &length)
{
    const unsigned char *source = face.font.data;
    if (face.type != Raster || !source || scale < 2 || scale > MaximumScale)
        return nullptr;
    const unsigned height = face.font.Height(), tall = height * scale;
    const unsigned glyphs = unsigned(source[0x60]) - source[0x5f] + 1;
    const unsigned table = 148, name_at = table + (glyphs + 1) * 6;
    unsigned name_length = 0;
    while (face.name[name_length])
        ++name_length;
    unsigned total = name_at + name_length + 1;
    for (unsigned i = 0; i < glyphs; ++i)
        total += ((face.font.Width(i) * scale + 7) / 8) * tall;
    if (tall > 128 || total > 8u * 1024 * 1024)
        return nullptr;
    auto *out = static_cast<unsigned char *>(calloc(total + tall, 1));
    if (!out)
        return nullptr;
    memcpy(out, source, 118);
    Put16(out, 0x300);
    Put32(out + 2, total);
    // dfPoints, dfAscent, both leadings, dfPixWidth, dfPixHeight, dfAvgWidth, dfMaxWidth.
    static const unsigned char sizes[] = {0x44, 0x4a, 0x4c, 0x4e, 0x56, 0x58, 0x5b, 0x5d};
    for (unsigned at : sizes)
        Put16(out + at, Word(source + at) * scale);
    Put32(out + 0x65, 0);                                  // dfDevice
    Put32(out + 0x69, name_at);                            // dfFace
    Put32(out + 0x6d, 0);                                  // dfBitsPointer
    Put32(out + 0x71, name_at + name_length + 1);          // dfBitsOffset
    Put32(out + 0x76, Word(source + 0x56) ? 1u : 2u);      // dfFlags: fixed or proportional
    memcpy(out + name_at, face.name, name_length + 1);
    unsigned at = name_at + name_length + 1;
    for (unsigned i = 0; i < glyphs; ++i)
    {
        const unsigned width = face.font.Width(i), wide = width * scale, bits = face.font.Bits(i);
        Put16(out + table + i * 6, wide);
        Put32(out + table + i * 6 + 2, at);
        for (unsigned x = 0; x < width; ++x)
            for (unsigned y = 0; y < height; ++y)
            {
                if (!(source[bits + (x / 8) * height + y] & (0x80u >> (x % 8))))
                    continue;
                for (unsigned dx = 0; dx < scale; ++dx)
                    for (unsigned dy = 0; dy < scale; ++dy)
                    {
                        const unsigned px = x * scale + dx, py = y * scale + dy;
                        out[at + (px / 8) * tall + py] |= (unsigned char)(0x80u >> (px % 8));
                    }
            }
        at += ((wide + 7) / 8) * tall;
    }
    // The entry after the last character, an empty glyph at the end.
    Put16(out + table + glyphs * 6, 0);
    Put32(out + table + glyphs * 6 + 2, at);
    length = total;
    return out;
}

bool SameFace(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        if (Upper(*a) != Upper(*b))
            return false;
        if (!*a)
            return true;
    }
}
void Reset()
{
    for (unsigned i = 0; i < count; ++i)
        free(owned[i]), owned[i] = nullptr;
    count = substitute_count = 0;
    for (auto &role : roles)
        role = -1;
}
unsigned Count() { return count; }
const Face *At(unsigned index) { return index < count ? faces + index : nullptr; }
bool Installed(const char *file)
{
    for (unsigned i = 0; i < count; ++i)
        if (SameFace(faces[i].file, file))
            return true;
    return false;
}
// An NE font module: the resource table at NE+24h starts with the alignment
// shift, followed by TYPEINFO records (type, count, reserved DWORD) and their
// NAMEINFO entries (offset and length in alignment units, flags, id, two
// reserved words). RT_FONT is 8 with the integer bit 8000h.
unsigned InstallFontFile(const char *file, const unsigned char *bytes, unsigned size)
{
    if (!bytes || size < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z')
        return 0;
    const unsigned ne = Dword(bytes + 0x3c);
    if (ne > size - 0x40 || bytes[ne] != 'N' || bytes[ne + 1] != 'E')
        return 0;
    unsigned at = ne + Word(bytes + ne + 0x24), added = 0;
    if (at + 2 > size)
        return 0;
    const unsigned shift = Word(bytes + at);
    at += 2;
    while (at + 8 <= size && Word(bytes + at))
    {
        const unsigned type = Word(bytes + at), entries = Word(bytes + at + 2);
        at += 8;
        for (unsigned i = 0; i < entries && at + 12 <= size; ++i, at += 12)
        {
            if (type != 0x8008 || shift > 16)
                continue;
            const unsigned offset = Word(bytes + at) << shift, length = Word(bytes + at + 2) << shift;
            if (offset < size && length && length <= size - offset &&
                Add(file, bytes + offset, length, true, Raster, 0))
                ++added;
        }
    }
    return added;
}
bool InstallFnt(const char *file, const unsigned char *fnt, unsigned size, bool copy)
{
    return Add(file, fnt, size, copy, Raster, 0);
}
unsigned InstallTrueType(const char *file)
{
    const GdiTrueType::Face *tt = GdiTrueType::Install(file);
    if (!tt || count == Capacity)
        return 0;
    const GdiTrueType::Font *font = GdiTrueType::Realize(tt, 0, 0, tt->weight, tt->italic, false, false, true);
    if (!font)
        return 0;
    Face face;
    face.font = font->view;
    if (!face.font.FaceName(face.name))
        return 0;
    face.type = TrueType;
    face.ppem = unsigned(font->ppem);
    face.truetype = tt;
    Copy(face.file, tt->leaf, sizeof face.file);
    owned[count] = nullptr;
    faces[count++] = face;
    return 1;
}
void SetRole(Role role, const char *file)
{
    for (unsigned i = 0; i < count; ++i)
        if (faces[i].type == Raster && SameFace(faces[i].file, file))
        {
            roles[role] = int(i);
            return;
        }
}
const Face *RoleFace(Role role) { return roles[role] < 0 ? nullptr : faces + roles[role]; }
void AddSubstitute(const char *from, const char *to)
{
    if (substitute_count == SubstituteCapacity || !*from || !*to)
        return;
    Copy(substitutes[substitute_count].from, from, 32);
    Copy(substitutes[substitute_count].to, to, 32);
    ++substitute_count;
}
const char *Substitute(const char *face)
{
    for (unsigned i = 0; i < substitute_count; ++i)
        if (SameFace(substitutes[i].from, face))
            return substitutes[i].to;
    return face;
}

// The native font mapper prefers the face name, then the character set,
// size, weight and italic style. Among the sizes of a raster face it weighs
// the height with the penalties of GDI's mapper, the table GDI.EXE carries: a
// size that is too high costs 600 and 150 for every pixel above the request
// (HeightBigger, HeightBiggerDifference), one that is too low 150 for every
// pixel missing (HeightSmaller), and a size enlarged by a whole factor 50 and
// 20 times the factor (SizeSynth, IntSizeSynth). So a request between two
// sizes takes the lower one, and one far above the largest size the nearest
// multiple of a small one. The other penalties of the table (family, pitch,
// width, aspect) are not weighed.
const Face *Match(const char *requested, int height, unsigned weight, bool italic, unsigned charset,
                  unsigned *scale)
{
    if (scale)
        *scale = 1;
    const char *name = Substitute(requested);
    bool raster = false, outline = false, charset_seen = false;
    for (unsigned i = 0; i < count; ++i)
        if (SameFace(faces[i].name, name))
        {
            (faces[i].type == TrueType ? outline : raster) = true;
            charset_seen |= faces[i].font.data[0x55] == charset;
        }
    if (!raster && !outline)
        return nullptr;
    if (!weight)
        weight = 400;
    const Face *best = nullptr;
    // Prefer a matching installed raster strike; use the outline family
    // when a different size would require raster scaling.
    if (raster)
    {
        // A raster face comes in fixed sizes. A negative height is the em
        // height, the cell without the internal leading. Zero requests a
        // 12-point em at the font size of the display, 16 pixels at 96 dots
        // per inch. The size with the least penalty wins, the smaller one on
        // a tie, then the nearer weight.
        const int target = height ? (height < 0 ? -height : height) : Gdi::PointsToPixels(12);
        int best_size = 0x7fffffff, best_weight = 0x7fffffff, best_cell = 0;
        unsigned best_scale = 1;
        for (unsigned i = 0; i < count; ++i)
        {
            const Face &f = faces[i];
            if (f.type != Raster || !SameFace(f.name, name) ||
                (charset_seen && charset != 1 && f.font.data[0x55] != charset))
                continue;
            const int w = Distance(int(Word(f.font.data + 0x53)), int(weight));
            for (unsigned factor = 1; factor <= (scale ? MaximumScale : 1u); ++factor)
            {
                // The enlarged cell has to stay one DibText draws.
                if (factor > 1 && (f.font.Height() * factor > 128 || Word(f.font.data + 0x5d) * factor > 256))
                    break;
                const int cell = int(f.font.Height() * factor);
                const int size = cell - (height <= 0 ? int(Word(f.font.data + 0x4c) * factor) : 0);
                int d = size > target ? 600 + 150 * (size - target) : 150 * (target - size);
                if (factor > 1)
                    d += 50 + 20 * int(factor);
                if (!best || d < best_size || (d == best_size && cell < best_cell) ||
                    (d == best_size && cell == best_cell && w < best_weight))
                    best = &f, best_size = d, best_weight = w, best_cell = cell, best_scale = factor;
            }
        }
        if (!outline || best_size == 0)
        {
            if (scale)
                *scale = best_scale;
            return best;
        }
        best=nullptr;
    }
    // A TrueType family is a set of files (regular, bold,...). The file with
    // the requested slant and the nearest weight wins, the lighter on a tie;
    // the caller realizes it at the requested size (src/gdi_truetype.cpp).
    (void)height;
    int file_score = 0x7fffffff;
    for (unsigned i = 0; i < count; ++i)
    {
        const Face &f = faces[i];
        if (f.type != TrueType || !SameFace(f.name, name))
            continue;
        const int w = int(Word(f.font.data + 0x53));
        const int score = (bool(f.font.data[80]) != italic) * 100000 + Distance(w, int(weight)) * 2 + (w > int(weight));
        if (score < file_score)
            best = &f, file_score = score;
    }
    return best;
}
} // namespace GdiFonts
