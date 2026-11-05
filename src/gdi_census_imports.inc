// SPDX-License-Identifier: GPL-2.0-or-later
// GDI answers that the imported exports need and no other part
// of this port asks for.

// The colour table DIBENG walks in EnumObj : it follows the
// far pointer at PDEVICE +20h, takes the count at +20h of that record, caps it
// at `1 << deBitsPixel` and reads the table at the offset in its first word. A
// surface deeper than eight bits gets a count of zero there, because the shift
// is skipped for `deBitsPixel > 8`, and then EnumObj enumerates nothing at all.
// The answer is the number of entries written, or ~0u for a palettized surface
// whose colour table this port does not keep.
unsigned SurfaceColours(HDC handle, unsigned *table, unsigned capacity)
{
    auto *c = DC(handle);
    if (!c) return 0;
    auto *object = Find(c->current.bitmap, BitmapObject);
    if (!object) return 0;
    const unsigned depth = unsigned(object->bitmap.depth);
    if (depth > 8) return 0;
    if (!object->dib)
    {
        // DIBENG's monochrome surface carries the two ends of the range.
        if (depth != 1 || capacity < 2) return ~0u;
        table[0] = 0;
        table[1] = 0x00ffffff;
        return 2;
    }
    const auto &format = object->dib->format;
    unsigned count = format.colors;
    if (count > capacity) count = capacity;
    if (count) memcpy(table, format.info + format.palette, count * 4);
    return count;
}
