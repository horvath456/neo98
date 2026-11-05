// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace DibEng {
using Offset = __PTRDIFF_TYPE__;
using Size = __SIZE_TYPE__;
// ARM representation of a linear DIBENGINE. bits addresses logical row zero;
// stride may be negative. Selectors, driver callbacks and banking are absent.
struct Bitmap {
    unsigned char *bits;
    int width, height, stride, depth;
};
struct Brush {
    unsigned color;
    const unsigned *pattern = nullptr; // optional 8x8 pixels, already realized
    // A DIB pattern brush over 8 by 8 pixels: the whole DIB, realized, and the
    // surface point where its top-left pixel lies. GDI tiles it for a
    // raster operation that uses the pattern and not the source.
    const unsigned *tile = nullptr;
    int tile_width = 0, tile_height = 0, tile_x = 0, tile_y = 0;
};
bool Valid(const Bitmap &);
// One pixel. Inline, because the raster paths call them per pixel.
inline unsigned ReadPixel(const Bitmap &b, int x, int y)
{
    const unsigned char *p = b.bits + (Offset)y * b.stride;
    switch (b.depth) {
    case 1: return (p[x >> 3] >> (7 - (x & 7))) & 1;
    case 4: return (p[x >> 1] >> ((1 - (x & 1)) * 4)) & 15;
    case 8: return p[x];
    case 16: p += x * 2; return p[0] | (unsigned)p[1] << 8;
    case 24: p += x * 3; return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16;
    default: { unsigned value; __builtin_memcpy(&value, p + x * 4, 4); return value; }
    }
}
inline void WritePixel(Bitmap &b, int x, int y, unsigned value)
{
    unsigned char *p = b.bits + (Offset)y * b.stride;
    switch (b.depth) {
    case 1: { const unsigned shift = 7 - (x & 7); p[x >> 3] = (p[x >> 3] & ~(1u << shift)) | ((value & 1) << shift); break; }
    case 4: { const unsigned shift = (1 - (x & 1)) * 4; p[x >> 1] = (p[x >> 1] & ~(15u << shift)) | ((value & 15) << shift); break; }
    case 8: p[x] = value; break;
    case 16: p += x * 2; p[0] = value; p[1] = value >> 8; break;
    case 24: p += x * 3; p[0] = value; p[1] = value >> 8; p[2] = value >> 16; break;
    default: __builtin_memcpy(p + x * 4, &value, 4); break;
    }
}
// This is the driver-level operation. GDI must clip the destination first.
// The original source clipping and overlap traversal live here.
bool BitBlt(Bitmap &destination, int x, int y, int width, int height,
            const Bitmap *source, int source_x, int source_y,
            unsigned rop, const Brush &brush);
unsigned ApplyRop(unsigned rop_index, unsigned source, unsigned dest, unsigned pattern);
bool UsesSource(unsigned rop);
bool UsesPattern(unsigned rop);
unsigned OriginalSelfTest();
unsigned OriginalTestCount();
}
