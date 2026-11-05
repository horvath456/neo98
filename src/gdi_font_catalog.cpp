// SPDX-License-Identifier: GPL-2.0-or-later
#include "gdi_font_catalog.h"
#include <stdlib.h>
#include <string.h>
#include "gdi_truetype.h"
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
} // namespace

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
// size, weight and italic style. It does not use GDI's penalty tables.
const Face *Match(const char *requested, int height, unsigned weight, bool italic, unsigned charset)
{
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
        // 12-point em at the display DPI. The nearest size wins, the smaller one
        // on a tie, then the nearer weight.
        const int target = height ? (height < 0 ? -height : height) : 16;
        int best_size = 0x7fffffff, best_weight = 0x7fffffff;
        for (unsigned i = 0; i < count; ++i)
        {
            const Face &f = faces[i];
            if (f.type != Raster || !SameFace(f.name, name) ||
                (charset_seen && charset != 1 && f.font.data[0x55] != charset))
                continue;
            const int cell = int(f.font.Height()) - (height <= 0 ? int(Word(f.font.data + 0x4c)) : 0);
            const int d = Distance(cell, target), w = Distance(int(Word(f.font.data + 0x53)), int(weight));
            const int best_cell = best ? int(best->font.Height()) : 0;
            if (!best || d < best_size || (d == best_size && int(f.font.Height()) < best_cell) ||
                (d == best_size && int(f.font.Height()) == best_cell && w < best_weight))
                best = &f, best_size = d, best_weight = w;
        }
        if(!outline || best_size==0)return best;
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
