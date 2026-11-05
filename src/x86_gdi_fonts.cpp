// SPDX-License-Identifier: GPL-2.0-or-later
// The remaining font exports of the import list. Windows 98 answers four of
// them from a single test: GDI asks whether the realized font is an
// engine font; every raster font fails it, a TrueType font passes it and is
// answered by Win98's font driver (src/gdi_truetype.cpp). The three that do
// work on a raster font are rebuilt from their originals.
#include "x86_app.h"
#include "gdi_font_catalog.h"
#include "gdi_truetype.h"
#include <string.h>
#include <stdlib.h>
namespace X86 {
namespace {
// GDI and its neighbours: the Win32 adapter inside GDI.EXE compares the
// answer of the 16-bit core against its failure value and only then calls
// KERNEL SetLastError with 57h.
constexpr unsigned NotAnOutlineFont = 87;
} // namespace

// GetTextExtentExPointA, GDI. The worker measures one prefix after the
// other with GETTEXTEXTENTPOINT, so every entry of alpDx is the
// extent of the whole prefix and not a single character width. The loop stops
// at the first prefix wider than nMaxExtent, and the comparison is `JA`, so a
// prefix exactly as wide as nMaxExtent still counts. Without lpnFit the limit
// is ignored: Windows 98 sets it to FFFFFFFFh. lpSize always describes the whole
// string, which is why measures once more when not everything fitted.
unsigned Application::GdiTextExtentExApi(ApiName, const unsigned *a)
{
    const unsigned dc = a[0], text = a[1];
    const int count = int(a[2]);
    if (!Gdi::ValidDC(dc) || count < 0 || (count && !text) || !a[6])
        return 0;
    unsigned limit = a[3];
    if (!a[4])
        limit = ~0u;
    auto *bytes = count ? new char[unsigned(count)] : nullptr;
    if (count && (!bytes || !Read(text, bytes, unsigned(count))))
    {
        delete[] bytes;
        last_error = 87;
        return 0;
    }
    Gdi::SIZE size{0, 0};
    int fits = 0;
    bool ok = true;
    for (int i = 1; ok && i <= count; ++i)
    {
        if (!Gdi::GetTextExtentPoint32A(dc, bytes, i, &size))
        {
            ok = false;
            break;
        }
        if (unsigned(size.cx) > limit)
            break;
        if (a[5] && !Store(a[5] + unsigned(i - 1) * 4, &size.cx, 4))
        {
            ok = false;
            break;
        }
        fits = i;
    }
    if (ok && a[4] && !Store(a[4], &fits, 4))
        ok = false;
    if (ok && fits != count && !Gdi::GetTextExtentPoint32A(dc, bytes, count, &size))
        ok = false;
    delete[] bytes;
    if (!ok)
        return 0;
    const int wire[2] = {size.cx, size.cy};
    return Store(a[6], wire, sizeof wire) ? 1 : 0;
}

// SetMapperFlags, GDI. Windows 98 rejects every bit except bit 0 with
// GDI_ERROR; exchanges the DWORD at DC+A4h and marks the font for
// a fresh realization. The flag itself selects aspect ratio filtering in the
// font mapper, which NEO98 does not have; only the stored value is visible.
unsigned Application::GdiMapperFlagsApi(ApiName, const unsigned *a)
{
    if (a[1] & ~1u)
        return ~0u;
    unsigned previous = 0;
    const unsigned value = a[1];
    if (!Gdi::MapperFlags(a[0], nullptr, previous))
        return 0;
    // before the exchange: the aspect ratio filter of the DC. Without
    // ASPECT_FILTERING it is zero. With it, the font resource list at
    // [1F4h]:[6] is searched for the raster resource whose resolution lies
    // closest to the device's logical resolution (GDIINFO+58h, +5Ah), by the
    // square of the distance, the first of equals winning; the system font's
    // resource (DGROUP 1FAh) and the Terminal faces (1FEh) do not count.
    // Nothing found leaves 7FFFh in both.
    int filter[2] = {0, 0};
    if (value & 1)
    {
        const int lx = int(Gdi::DeviceCaps(a[0], 88)), ly = int(Gdi::DeviceCaps(a[0], 90));
        long long best = 0x7fffffffll;
        filter[0] = filter[1] = 0x7fff;
        const auto *system = GdiFonts::RoleFace(GdiFonts::SystemRole);
        for (unsigned i = 0; i < GdiFonts::Count(); ++i)
        {
            const auto *face = GdiFonts::At(i);
            if (!face || face->type != GdiFonts::Raster || !face->font.data || face->font.length < 0x4a)
                continue;
            if (system && !strcmp(face->file, system->file))
                continue;
            if (GdiFonts::SameFace(face->name, "Terminal"))
                continue;
            const unsigned char *fnt = face->font.data;
            const int vertical = fnt[0x46] | fnt[0x47] << 8, horizontal = fnt[0x48] | fnt[0x49] << 8;
            const long long dx = horizontal - lx, dy = vertical - ly, distance = dx * dx + dy * dy;
            if (distance < best)
            {
                best = distance;
                filter[0] = horizontal;
                filter[1] = vertical;
            }
        }
    }
    int x = 0, y = 0;
    Gdi::AspectFilter(a[0], filter, x, y);
    return Gdi::MapperFlags(a[0], &value, previous) ? previous : 0;
}

// GetCharacterPlacementA, GDI32 over GDI. asks the
// realized font for its own placement vector; a font without one, which is
// every raster font, takes the generic Latin path. That path measures
// the string with the Win16 GetTextExtentEx core and fills the
// result: lpOutString is the input copied over, lpOrder is the identity,
// lpCaretPos are the running sums of lpDx, lpClass is GCPCLASS_LATIN
// everywhere, and lpGlyphs are the character codes, because Windows 98 finds no
// glyph index table in a raster font. GCP_REORDER, GCP_LIGATE, GCP_GLYPHSHAPE,
// GCP_DIACRITIC and GCP_KASHIDA are ignored without a diagnosis, and
// GCP_USEKERNING falls away because GetKerningPairs answers zero.
unsigned Application::GdiCharacterPlacementApi(ApiName, const unsigned *a)
{
    const unsigned dc = a[0], text = a[1], results = a[4];
    const int count = int(a[2]);
    if (!Gdi::ValidDC(dc) || count < 0 || (count && !text) || !results)
    {
        last_error = 87;
        return 0;
    }
    unsigned header[9];
    if (!Read(results, header, sizeof header))
    {
        last_error = 87;
        return 0;
    }
    // Windows 98 rejects a structure whose size field is not the expected one.
    if (header[0] != sizeof header)
    {
        last_error = 87;
        return 0;
    }
    auto *bytes = new char[unsigned(count) + 1];
    auto *widths = new int[unsigned(count) + 1];
    if (!bytes || !widths || (count && !Read(text, bytes, unsigned(count))))
    {
        delete[] bytes;
        delete[] widths;
        last_error = 87;
        return 0;
    }
    bytes[count] = 0;
    Gdi::SIZE size{0, 0};
    bool ok = Gdi::GetTextExtentPoint32A(dc, bytes, count, &size);
    int previous = 0;
    for (int i = 1; ok && i <= count; ++i)
    {
        Gdi::SIZE prefix{0, 0};
        ok = Gdi::GetTextExtentPoint32A(dc, bytes, i, &prefix);
        widths[i - 1] = prefix.cx - previous;
        previous = prefix.cx;
    }
    const unsigned glyphs = unsigned(count);
    for (unsigned i = 0; ok && i < glyphs; ++i)
    {
        if (header[3] && !Store(header[3] + i * 4, widths + i, 4))
            ok = false;
        if (header[2] && ok)
        {
            const unsigned order = i;
            ok = Store(header[2] + i * 4, &order, 4);
        }
        if (header[5] && ok)
        {
            const unsigned char latin = 1;
            ok = Store(header[5] + i, &latin, 1);
        }
        if (header[6] && ok)
        {
            const unsigned short glyph = (unsigned char)bytes[i];
            ok = Store(header[6] + i * 2, &glyph, 2);
        }
    }
    if (ok && header[4])
    {
        int caret = 0;
        for (unsigned i = 0; ok && i < glyphs; ++i)
        {
            ok = Store(header[4] + i * 4, &caret, 4);
            caret += widths[i];
        }
    }
    if (ok && header[1] && count)
        ok = Store(header[1], bytes, unsigned(count));
    if (ok)
    {
        header[7] = glyphs; // nGlyphs
        header[8] = glyphs; // nMaxFit
        ok = Store(results + 28, header + 7, 8);
    }
    delete[] bytes;
    delete[] widths;
    if (!ok)
    {
        last_error = 87;
        return 0;
    }
    return (unsigned(size.cx) & 65535) | (unsigned(size.cy) << 16);
}

// The four outline queries. GDI tests bit 10h of the DC flag byte at
// DC+0Fh, the mark of a realized engine font, and falls back to bit 4 together
// with `(FONTINFO.dfType & 3) == 3`. A raster font carries dfType zero and
// fails both, so the core leaves through its failure exit: Windows 98 answers
// FFFFFFFFh for GetFontData and GetGlyphOutline, Windows 98 answers zero for
// GetOutlineTextMetrics, and Windows 98 answers zero for GetKerningPairs. The
// adapter in GDI segment 43 turns each of those into SetLastError(57h). A
// TrueType font (src/gdi_truetype.cpp) takes the engine path; the adapters
// widen the Win16 results as GDI does.
unsigned Application::GdiOutlineQueryApi(ApiName name, const unsigned *a)
{
    const bool counting = API_IS(name, "GetOutlineTextMetricsA") || API_IS(name, "GetKerningPairsA");
    if (!Gdi::ValidDC(a[0]))
    {
        last_error = 87;
        return counting ? 0 : ~0u;
    }
    const GdiTrueType::Font *font = GdiTrueType::FontOf(Gdi::SelectedFont(a[0]));
    auto fail = [&]() {
        last_error = NotAnOutlineFont;
        return counting ? 0u : ~0u;
    };
    if (!font)
        return fail();
    if (API_IS(name, "GetGlyphOutlineA"))
    {
        // over : the format may only carry the GGO
        // format bits and GGO_GLYPH_INDEX, the Win16 GLYPHMETRICS and
        // the MAT2 must be there, a buffer must be writable.
        const unsigned character = a[1] & 0xffff, format = a[2] & 0xffff, size = a[4], buffer = a[5];
        int matrix[4];
        if ((format & 0xff78) || !a[3] || !a[6] || !Read(a[6], matrix, sizeof matrix) ||
            (buffer && size && !virtual_memory.Access(buffer, size, true)))
            return fail();
        unsigned char *bytes = buffer && size ? static_cast<unsigned char *>(calloc(size, 1)) : nullptr;
        if (buffer && size && !bytes)
            return fail();
        GdiTrueType::GlyphMetrics gm;
        const unsigned result = GdiTrueType::GlyphOutline(*font, character, format, matrix, bytes, bytes ? size : 0, gm);
        if (result == ~0u)
        {
            free(bytes);
            return fail();
        }
        if (bytes && !Store(buffer, bytes, result < size ? result : size))
        {
            free(bytes);
            return fail();
        }
        free(bytes);
        // box unsigned, origin sign-extended, increments as
        // they are.
        const int wire[4] = {int(gm.black_x & 0xffff), int(gm.black_y & 0xffff), gm.origin_x, gm.origin_y};
        const short cells[2] = {gm.cell_x, gm.cell_y};
        if (!Store(a[3], wire, sizeof wire) || !Store(a[3] + 16, cells, sizeof cells))
            return fail();
        return result;
    }
    if (API_IS(name, "GetFontData"))
    {
        // Use entry 32 of the driver.
        const unsigned buffer = a[3], size = a[4];
        if (buffer && size && !virtual_memory.Access(buffer, size, true))
            return fail();
        unsigned char *bytes = buffer && size ? static_cast<unsigned char *>(malloc(size)) : nullptr;
        if (buffer && size && !bytes)
            return fail();
        const unsigned result = GdiTrueType::FontData(*font, a[1], a[2], bytes, bytes ? size : 0);
        if (result == ~0u || (bytes && !Store(buffer, bytes, result < size ? result : size)))
        {
            free(bytes);
            return fail();
        }
        free(bytes);
        return result;
    }
    if (API_IS(name, "GetOutlineTextMetricsA"))
    {
        // The mapping modes other than
        // MM_TEXT, which scales, are not implemented.
        Gdi::TEXTMETRICA tm{};
        if (!Gdi::GetTextMetricsA(a[0], &tm))
            return fail();
        if (!a[2])
        {
            const unsigned need = GdiTrueType::OutlineTextMetrics(*font, tm, nullptr, 0);
            return need ? need : fail();
        }
        unsigned char *bytes = static_cast<unsigned char *>(calloc(a[1] ? a[1] : 1, 1));
        const unsigned result = bytes ? GdiTrueType::OutlineTextMetrics(*font, tm, bytes, a[1]) : 0;
        if (!result || (a[1] && !Store(a[2], bytes, a[1])))
        {
            free(bytes);
            return fail();
        }
        free(bytes);
        return result;
    }
    // GetKerningPairsA: with a buffer
    // the count must be 1..FFFFh; the six-byte Win16 pairs become KERNINGPAIR
    // records with the amount widened to an int.
    const unsigned count = a[1], pairs = a[2];
    if (pairs && (!count || count >= 0x10000))
        return fail();
    unsigned char *bytes = pairs ? static_cast<unsigned char *>(calloc(count, 6)) : nullptr;
    if (pairs && !bytes)
        return fail();
    const unsigned result = GdiTrueType::KerningPairs(*font, count & 0xffff, bytes);
    const unsigned written = result < count ? result : count;
    for (unsigned i = 0; pairs && i < written; ++i)
    {
        const unsigned short first = bytes[i * 6] | bytes[i * 6 + 1] << 8, second = bytes[i * 6 + 2] | bytes[i * 6 + 3] << 8;
        const int amount = short(bytes[i * 6 + 4] | bytes[i * 6 + 5] << 8);
        unsigned char record[8];
        memcpy(record, &first, 2);
        memcpy(record + 2, &second, 2);
        memcpy(record + 4, &amount, 4);
        if (!Store(pairs + i * 8, record, 8))
        {
            free(bytes);
            return fail();
        }
    }
    free(bytes);
    return result ? (result & 0xffff) : fail();
}
} // namespace X86
