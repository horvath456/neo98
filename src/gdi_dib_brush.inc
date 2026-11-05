// SPDX-License-Identifier: GPL-2.0-or-later
// The DIB pattern brush: GDI creates it, GDI realizes it.
// GDI32 CreateDIBPatternBrushPt computes the packed size of the
// DIB and thunks with CL 33h to GDI, which copies that many bytes, as
// whole DWORDs, into a global block of its own and makes a brush of style 5
// (BS_DIBPATTERN) with the usage word and the block. GetObject answers
// {5, iUsage, 0} for it.
// At realization the brush becomes a pattern brush: GDI builds a
// device bitmap from the DIB with CreateDIBitmap, so the colours go
// through the ordinary DIB-to-device conversion and DIB_PAL_COLORS indexes the
// palette of the DC. The pattern bitmap must be at least 8 by 8 pixels, otherwise the brush does not realize. A two-colour DIB
// with DIB_RGB_COLORS on a monochrome surface (DC+9Ch = 0101h) is realized as
// black for index 0 and white for index 1, whatever its colour table says.
// The driver takes the top-left 8 by 8 pixels of the device bitmap, that is
// the top eight rows of the image.
// A brush larger than 8 by 8 also marks the DC when it is selected (
// sets DC+0Fh bit 20h), and for a raster operation that uses the pattern but
// not the source GDI tiles the whole DIB itself. That path is not
// part of this port yet: every operation here uses the top-left 8 by 8
// pixels.
// The.inc sits inside the anonymous namespace of gdi.cpp; the public entry
// Gdi::CreateDibPatternBrush next to CreatePatternBrush calls in here.
namespace {
// One pixel of a DIB as COLORREF, or as its index for a palette DIB.
struct DibReader {
    const unsigned char *dib = nullptr;
    unsigned size = 0, header = 0, width = 0, height = 0, depth = 0, compression = 0;
    unsigned colors = 0, usage = 0, stride = 0, masks[3] = {};
    const unsigned char *table = nullptr, *bits = nullptr;
    unsigned char *expanded = nullptr; // RLE images, one byte per pixel
    ~DibReader() { delete[] expanded; }
    static unsigned Word(const unsigned char *p) { return p[0] | p[1] << 8; }
    static unsigned Long(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
    bool Open(const unsigned char *data, unsigned bytes, unsigned use) {
        dib = data; size = bytes; usage = use;
        header = Long(data);
        if (header == 12) {
            width = Word(data + 4); height = Word(data + 6); depth = Word(data + 10);
            compression = 0;
            colors = depth <= 8 ? 1u << depth : 0;
            table = data + 12;
            bits = table + colors * (usage ? 2 : 3);
        } else {
            width = Long(data + 4); height = Long(data + 8); depth = Word(data + 14);
            compression = Long(data + 16);
            const unsigned used = Long(data + 32);
            colors = depth <= 8 ? (used && used <= (1u << depth) ? used : 1u << depth) : 0;
            table = data + header;
            bits = table + colors * (usage ? 2 : 4);
            if (compression == 3) {
                masks[0] = Long(data + header); masks[1] = Long(data + header + 4); masks[2] = Long(data + header + 8);
                bits = data + header + 12;
            }
        }
        if (!width || !height || (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32))
            return false;
        stride = ((width * depth + 31) >> 3) & ~3u;
        if (compression == 1 || compression == 2) {
            if ((compression == 1 && depth != 8) || (compression == 2 && depth != 4)) return false;
            const unsigned row = depth == 8 ? width : (width + 1) / 2;
            expanded = new unsigned char[row * height + width * height];
            memset(expanded, 0, row * height + width * height);
            Win98DibRle::Target target{expanded, expanded + row * height, width, height, row, depth};
            const unsigned length = unsigned(dib + size - bits);
            if (!Win98DibRle::Decode(bits, length, target)) return false;
            bits = expanded;
            stride = row;
        } else if (compression != 0 && compression != 3)
            return false;
        return bits <= dib + size;
    }
    // x from the left, y from the top of the picture.
    unsigned Raw(unsigned x, unsigned y) const {
        const unsigned row = height - 1 - y;
        const unsigned char *line = bits + row * stride;
        if (line + stride > dib + size && !expanded) return 0;
        switch (depth) {
        case 1: return (line[x >> 3] >> (7 - (x & 7))) & 1;
        case 4: return (line[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
        case 8: return line[x];
        case 16: return Word(line + x * 2);
        case 24: return line[x * 3] | line[x * 3 + 1] << 8 | line[x * 3 + 2] << 16;
        default: return Long(line + x * 4);
        }
    }
    static unsigned Channel(unsigned value, unsigned mask) {
        if (!mask) return 0;
        unsigned shift = 0;
        while (!(mask & 1)) { mask >>= 1; ++shift; }
        unsigned bits = 0;
        while (mask & (1u << bits)) ++bits;
        const unsigned v = (value >> shift) & ((1u << bits) - 1);
        // The expansion of DIBENG's generated conversions (win98_dib_raster.h).
        return bits >= 8 ? v >> (bits - 8) : Win98DibRaster::Expand(v, bits);
    }
    // COLORREF of an RGB DIB.
    unsigned Color(unsigned x, unsigned y) const {
        const unsigned value = Raw(x, y);
        if (depth <= 8) {
            if (value >= colors) return 0;
            const unsigned char *entry = table + value * (header == 12 ? 3 : 4);
            return entry[2] | entry[1] << 8 | entry[0] << 16;
        }
        if (depth == 16) {
            const unsigned r = compression == 3 ? Channel(value, masks[0]) : Channel(value, 0x7c00);
            const unsigned g = compression == 3 ? Channel(value, masks[1]) : Channel(value, 0x03e0);
            const unsigned b = compression == 3 ? Channel(value, masks[2]) : Channel(value, 0x001f);
            return r | g << 8 | b << 16;
        }
        if (depth == 32 && compression == 3)
            return Channel(value, masks[0]) | Channel(value, masks[1]) << 8 | Channel(value, masks[2]) << 16;
        return ((value >> 16) & 255) | (value & 0xff00) | (value & 255) << 16;
    }
    // The palette index a DIB_PAL_COLORS table names for a pixel.
    unsigned PaletteIndex(unsigned x, unsigned y) const {
        const unsigned value = Raw(x, y);
        return value < colors ? Word(table + value * 2) : 0;
    }
};
} // namespace

// GDI over the packed DIB GDI32 measured. `size` is that measure.
HBRUSH CreateDibPatternBrushObject(const unsigned char *dib, unsigned size, unsigned usage)
{
    if (!dib || size < 12 || usage > 1)
        return 0;
    const HBRUSH handle = Allocate(BrushObject);
    if (!handle)
        return 0;
    auto *o = Find(handle, BrushObject);
    // `rep movsd` over size >> 2 DWORDs: the last size & 3 bytes are not
    // copied; the block is not zeroed by GlobalAlloc(2002h), the port keeps
    // zero there.
    o->allocation = new unsigned char[size];
    memset(o->allocation, 0, size);
    memcpy(o->allocation, dib, size & ~3u);
    o->dib_brush = true;
    o->dib_usage = usage;
    o->dib_bytes = size;
    DibReader reader;
    o->dib_realizable = reader.Open(o->allocation, size, usage) && reader.width >= 8 && reader.height >= 8;
    if (o->dib_realizable) {
        o->dib_width = int(reader.width);
        o->dib_height = int(reader.height);
    }
    if (o->dib_realizable) {
        o->dib_two_colors = reader.colors == 2;
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                o->dib_index[y * 8 + x] = (unsigned char)(usage ? reader.PaletteIndex(x, y) : reader.Raw(x, y));
                o->pattern[y * 8 + x] = usage ? 0 : Pixel(reader.Color(x, y));
            }
    }
    o->brush.pattern = o->pattern;
    o->pattern_ready = o->dib_realizable;
    return handle;
}
// The pixels hands the driver, as 00RRGGBB before device conversion.
bool RealizeDibBrush(const Object &brush, const State &state, const Object &surface, unsigned (&pixels)[64])
{
    if (!brush.dib_realizable)
        return false;
    const bool monochrome = surface.bitmap.depth == 1;
    if (!brush.dib_usage && brush.dib_two_colors && monochrome) {
        for (unsigned i = 0; i < 64; ++i)
            pixels[i] = brush.dib_index[i] ? 0xffffff : 0;
        return true;
    }
    if (!brush.dib_usage) {
        memcpy(pixels, brush.pattern, sizeof pixels);
        return true;
    }
    const Object *palette = Find(state.palette, PaletteObject);
    for (unsigned i = 0; i < 64; ++i) {
        unsigned color = 0;
        if (palette && brush.dib_index[i] < palette->palette.count)
            color = palette->palette.entries[brush.dib_index[i]].color & 0xffffff;
        pixels[i] = Pixel(color);
    }
    return true;
}
// The whole DIB of a brush over 8 by 8 pixels, as COLORREF values for
// RealizeBrush (RGB usage) or through the DC's palette (index usage).
bool RealizeDibTile(const Object &brush, const State &state)
{
    DibReader reader;
    if (!reader.Open(brush.allocation, brush.dib_bytes, brush.dib_usage))
        return false;
    const unsigned width = unsigned(brush.dib_width), height = unsigned(brush.dib_height);
    if (!brush.dib_tile)
        brush.dib_tile = new unsigned[width * height];
    const Object *palette = brush.dib_usage ? Find(state.palette, PaletteObject) : nullptr;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            unsigned color = 0;
            if (!brush.dib_usage)
                color = reader.Color(x, y);
            else {
                const unsigned index = reader.PaletteIndex(x, y);
                if (palette && index < palette->palette.count)
                    color = palette->palette.entries[index].color & 0xffffff;
            }
            brush.dib_tile[y * width + x] = color;
        }
    return true;
}
