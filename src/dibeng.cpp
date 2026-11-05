// SPDX-License-Identifier: GPL-2.0-or-later
// Raster operations on device-independent bitmaps.
#include "dibeng.h"
#include "rop_truth_table.h"
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/dibeng_vectors.h"
#endif

namespace DibEng {
bool Valid(const Bitmap &b)
{
    if (!b.bits || b.width <= 0 || b.height <= 0 || b.width > 32767 || b.height > 32767) return false;
    if (b.depth != 1 && b.depth != 4 && b.depth != 8 && b.depth != 16 && b.depth != 24 && b.depth != 32) return false;
    const long long stride = b.stride < 0 ? -(long long)b.stride : b.stride;
    return stride >= ((long long)b.width * b.depth + 7) / 8;
}


// ARM fast paths of the per-pixel loop below. Every recovered raster operation
// is a bitwise expression of source, destination and pattern (dibeng_rops.h),
// so for 8, 16 and 32 bits per pixel it gives the same bits when it runs over
// whole 32-bit words of a scanline, with the pattern colour repeated in each
// pixel of the word. The loops take the operation as a template argument, so
// the compiler inlines it and vectorises the loop (NEON on AArch64, also in
// the Pi kernel). Results are bit for bit those of the pixel loop; the
// original DIBENG vectors of OriginalSelfTest check that.
namespace {
using RowKernel = void (*)(unsigned char *, const unsigned char *, unsigned, Size);
template <RasterOp Op>
void Rows(unsigned char *d, const unsigned char *s, unsigned p, Size bytes)
{
    Size i = 0;
    for (; i + 4 <= bytes; i += 4) {
        unsigned dw, sw = 0;
        __builtin_memcpy(&dw, d + i, 4);
        if (s) __builtin_memcpy(&sw, s + i, 4);
        dw = Op(sw, dw, p);
        __builtin_memcpy(d + i, &dw, 4);
    }
    for (; i < bytes; ++i) d[i] = (unsigned char)Op(s ? s[i] : 0, d[i], p >> ((i & 3) * 8));
}
const RowKernel RowKernels[256] = {
    Rows<Rop00>,
    Rows<Rop01>,
    Rows<Rop02>,
    Rows<Rop03>,
    Rows<Rop04>,
    Rows<Rop05>,
    Rows<Rop06>,
    Rows<Rop07>,
    Rows<Rop08>,
    Rows<Rop09>,
    Rows<Rop0a>,
    Rows<Rop0b>,
    Rows<Rop0c>,
    Rows<Rop0d>,
    Rows<Rop0e>,
    Rows<Rop0f>,
    Rows<Rop10>,
    Rows<Rop11>,
    Rows<Rop12>,
    Rows<Rop13>,
    Rows<Rop14>,
    Rows<Rop15>,
    Rows<Rop16>,
    Rows<Rop17>,
    Rows<Rop18>,
    Rows<Rop19>,
    Rows<Rop1a>,
    Rows<Rop1b>,
    Rows<Rop1c>,
    Rows<Rop1d>,
    Rows<Rop1e>,
    Rows<Rop1f>,
    Rows<Rop20>,
    Rows<Rop21>,
    Rows<Rop22>,
    Rows<Rop23>,
    Rows<Rop24>,
    Rows<Rop25>,
    Rows<Rop26>,
    Rows<Rop27>,
    Rows<Rop28>,
    Rows<Rop29>,
    Rows<Rop2a>,
    Rows<Rop2b>,
    Rows<Rop2c>,
    Rows<Rop2d>,
    Rows<Rop2e>,
    Rows<Rop2f>,
    Rows<Rop30>,
    Rows<Rop31>,
    Rows<Rop32>,
    Rows<Rop33>,
    Rows<Rop34>,
    Rows<Rop35>,
    Rows<Rop36>,
    Rows<Rop37>,
    Rows<Rop38>,
    Rows<Rop39>,
    Rows<Rop3a>,
    Rows<Rop3b>,
    Rows<Rop3c>,
    Rows<Rop3d>,
    Rows<Rop3e>,
    Rows<Rop3f>,
    Rows<Rop40>,
    Rows<Rop41>,
    Rows<Rop42>,
    Rows<Rop43>,
    Rows<Rop44>,
    Rows<Rop45>,
    Rows<Rop46>,
    Rows<Rop47>,
    Rows<Rop48>,
    Rows<Rop49>,
    Rows<Rop4a>,
    Rows<Rop4b>,
    Rows<Rop4c>,
    Rows<Rop4d>,
    Rows<Rop4e>,
    Rows<Rop4f>,
    Rows<Rop50>,
    Rows<Rop51>,
    Rows<Rop52>,
    Rows<Rop53>,
    Rows<Rop54>,
    Rows<Rop55>,
    Rows<Rop56>,
    Rows<Rop57>,
    Rows<Rop58>,
    Rows<Rop59>,
    Rows<Rop5a>,
    Rows<Rop5b>,
    Rows<Rop5c>,
    Rows<Rop5d>,
    Rows<Rop5e>,
    Rows<Rop5f>,
    Rows<Rop60>,
    Rows<Rop61>,
    Rows<Rop62>,
    Rows<Rop63>,
    Rows<Rop64>,
    Rows<Rop65>,
    Rows<Rop66>,
    Rows<Rop67>,
    Rows<Rop68>,
    Rows<Rop69>,
    Rows<Rop6a>,
    Rows<Rop6b>,
    Rows<Rop6c>,
    Rows<Rop6d>,
    Rows<Rop6e>,
    Rows<Rop6f>,
    Rows<Rop70>,
    Rows<Rop71>,
    Rows<Rop72>,
    Rows<Rop73>,
    Rows<Rop74>,
    Rows<Rop75>,
    Rows<Rop76>,
    Rows<Rop77>,
    Rows<Rop78>,
    Rows<Rop79>,
    Rows<Rop7a>,
    Rows<Rop7b>,
    Rows<Rop7c>,
    Rows<Rop7d>,
    Rows<Rop7e>,
    Rows<Rop7f>,
    Rows<Rop80>,
    Rows<Rop81>,
    Rows<Rop82>,
    Rows<Rop83>,
    Rows<Rop84>,
    Rows<Rop85>,
    Rows<Rop86>,
    Rows<Rop87>,
    Rows<Rop88>,
    Rows<Rop89>,
    Rows<Rop8a>,
    Rows<Rop8b>,
    Rows<Rop8c>,
    Rows<Rop8d>,
    Rows<Rop8e>,
    Rows<Rop8f>,
    Rows<Rop90>,
    Rows<Rop91>,
    Rows<Rop92>,
    Rows<Rop93>,
    Rows<Rop94>,
    Rows<Rop95>,
    Rows<Rop96>,
    Rows<Rop97>,
    Rows<Rop98>,
    Rows<Rop99>,
    Rows<Rop9a>,
    Rows<Rop9b>,
    Rows<Rop9c>,
    Rows<Rop9d>,
    Rows<Rop9e>,
    Rows<Rop9f>,
    Rows<Ropa0>,
    Rows<Ropa1>,
    Rows<Ropa2>,
    Rows<Ropa3>,
    Rows<Ropa4>,
    Rows<Ropa5>,
    Rows<Ropa6>,
    Rows<Ropa7>,
    Rows<Ropa8>,
    Rows<Ropa9>,
    Rows<Ropaa>,
    Rows<Ropab>,
    Rows<Ropac>,
    Rows<Ropad>,
    Rows<Ropae>,
    Rows<Ropaf>,
    Rows<Ropb0>,
    Rows<Ropb1>,
    Rows<Ropb2>,
    Rows<Ropb3>,
    Rows<Ropb4>,
    Rows<Ropb5>,
    Rows<Ropb6>,
    Rows<Ropb7>,
    Rows<Ropb8>,
    Rows<Ropb9>,
    Rows<Ropba>,
    Rows<Ropbb>,
    Rows<Ropbc>,
    Rows<Ropbd>,
    Rows<Ropbe>,
    Rows<Ropbf>,
    Rows<Ropc0>,
    Rows<Ropc1>,
    Rows<Ropc2>,
    Rows<Ropc3>,
    Rows<Ropc4>,
    Rows<Ropc5>,
    Rows<Ropc6>,
    Rows<Ropc7>,
    Rows<Ropc8>,
    Rows<Ropc9>,
    Rows<Ropca>,
    Rows<Ropcb>,
    Rows<Ropcc>,
    Rows<Ropcd>,
    Rows<Ropce>,
    Rows<Ropcf>,
    Rows<Ropd0>,
    Rows<Ropd1>,
    Rows<Ropd2>,
    Rows<Ropd3>,
    Rows<Ropd4>,
    Rows<Ropd5>,
    Rows<Ropd6>,
    Rows<Ropd7>,
    Rows<Ropd8>,
    Rows<Ropd9>,
    Rows<Ropda>,
    Rows<Ropdb>,
    Rows<Ropdc>,
    Rows<Ropdd>,
    Rows<Ropde>,
    Rows<Ropdf>,
    Rows<Rope0>,
    Rows<Rope1>,
    Rows<Rope2>,
    Rows<Rope3>,
    Rows<Rope4>,
    Rows<Rope5>,
    Rows<Rope6>,
    Rows<Rope7>,
    Rows<Rope8>,
    Rows<Rope9>,
    Rows<Ropea>,
    Rows<Ropeb>,
    Rows<Ropec>,
    Rows<Roped>,
    Rows<Ropee>,
    Rows<Ropef>,
    Rows<Ropf0>,
    Rows<Ropf1>,
    Rows<Ropf2>,
    Rows<Ropf3>,
    Rows<Ropf4>,
    Rows<Ropf5>,
    Rows<Ropf6>,
    Rows<Ropf7>,
    Rows<Ropf8>,
    Rows<Ropf9>,
    Rows<Ropfa>,
    Rows<Ropfb>,
    Rows<Ropfc>,
    Rows<Ropfd>,
    Rows<Ropfe>,
    Rows<Ropff>};
// The pattern colour in every pixel of a 32-bit word.
bool Replicate(unsigned color, int depth, unsigned &word)
{
    switch (depth) {
    case 8: word = (color & 0xff) * 0x01010101u; return true;
    case 16: word = (color & 0xffff) * 0x00010001u; return true;
    case 32: word = color; return true;
    default: return false;
    }
}
} // namespace

bool UsesSource(unsigned rop) { return RasterInputs[(rop >> 16) & 255] & 2; }
bool UsesPattern(unsigned rop) { return RasterInputs[(rop >> 16) & 255] & 4; }
unsigned ApplyRop(unsigned index, unsigned s, unsigned d, unsigned p)
{
    return RasterOps[index & 255](s, d, p);
}

bool BitBlt(Bitmap &dst, int dx, int dy, int w, int h, const Bitmap *src,
            int sx, int sy, unsigned rop, const Brush &brush)
{
    if (rop & 0xff000000u || !Valid(dst) || w < 0 || h < 0 || w > 32767 || h > 32767) return false;
    if (dx < -32767 || dx > 32767 || dy < -32767 || dy > 32767) return false;
    const unsigned index = (rop >> 16) & 255;
    const bool source = RasterInputs[index] & 2;
    if (source) {
        if (!src || !Valid(*src) || src->depth != dst.depth) return false;
        // Source clipping also moves the destination origin.
        if (sx < 0) { if (sx < -w) return true; w += sx; dx -= sx; sx = 0; }
        if (sy < 0) { if (sy < -h) return true; h += sy; dy -= sy; sy = 0; }
        if (sx >= src->width || sy >= src->height) return true;
        if (w > src->width - sx) w = src->width - sx;
        if (h > src->height - sy) h = src->height - sy;
    }
    if (!w || !h) return true;
    // The Win98 driver assumes GDI has already clipped the destination.
    // The ARM boundary checks it instead of allowing writes outside the DIB.
    if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height ||
        w > dst.width - dx || h > dst.height - dy) return false;
    // Windows 98 selects bottom-right traversal for overlapping self blits.
    const bool reverse = source && src->bits == dst.bits && src->stride == dst.stride &&
        sx < dx + w && dx < sx + w && sy < dy + h && dy < sy + h &&
        (dy > sy || (dy == sy && dx > sx));
    if (reverse && dst.depth == 1) return false; // GDI must snapshot this source.
    const auto operation = RasterOps[index];
    const bool constant = !(RasterInputs[index] & 3) && !brush.pattern;
    const unsigned fill = constant ? operation(0, 0, brush.color) : 0;
    // A solid brush or none, 8/16/32 bits, no overlap with the source: whole
    // scanlines at once (see RowKernels).
    unsigned pattern_word = 0;
    if (!brush.pattern && !reverse && (!source || src->bits != dst.bits) &&
        Replicate(brush.color, dst.depth, pattern_word)) {
        const Size bytes = (Size)w * (dst.depth / 8);
        const auto kernel = RowKernels[index];
        for (int y = 0; y < h; ++y) {
            unsigned char *d = dst.bits + (Offset)(dy + y) * dst.stride + dx * (dst.depth / 8);
            const unsigned char *s =
                source ? src->bits + (Offset)(sy + y) * src->stride + sx * (src->depth / 8) : nullptr;
            if (index == 0xcc) __builtin_memmove(d, s, bytes);
            else kernel(d, s, pattern_word, bytes);
        }
        return true;
    }
    for (int row = 0; row < h; ++row) {
        const int y = reverse ? h - 1 - row : row;
        if (constant && dst.depth == 32) {
            for (int x = 0; x < w; ++x) WritePixel(dst, dx + x, dy + y, fill);
            continue;
        }
        if (index == 0xcc && dst.depth >= 8) {
            // Windows 98 copies scanlines after stride/alignment preparation.
            // Clang supplies ARM's alignment handling. Direction remains ours.
            const Size bytes = (Size)w * (dst.depth / 8);
            unsigned char *d = dst.bits + (Offset)(dy + y) * dst.stride + dx * (dst.depth / 8);
            const unsigned char *s = src->bits + (Offset)(sy + y) * src->stride + sx * (src->depth / 8);
            __builtin_memmove(d, s, bytes);
            continue;
        }
        for (int col = 0; col < w; ++col) {
            const int x = reverse ? w - 1 - col : col;
            const unsigned s = source ? ReadPixel(*src, sx + x, sy + y) : 0;
            const unsigned d = ReadPixel(dst, dx + x, dy + y);
            unsigned p = brush.color;
            if (brush.tile && !source) {
                int tx = (dx + x - brush.tile_x) % brush.tile_width, ty = (dy + y - brush.tile_y) % brush.tile_height;
                if (tx < 0) tx += brush.tile_width;
                if (ty < 0) ty += brush.tile_height;
                p = brush.tile[ty * brush.tile_width + tx];
            } else if (brush.pattern)
                p = brush.pattern[((dy + y) & 7) * 8 + ((dx + x) & 7)];
            WritePixel(dst, dx + x, dy + y, operation(s, d, p));
        }
    }
    return true;
}

#ifndef NEO98_RUNTIME_CONTENT
static unsigned Hash(const unsigned char *data, unsigned length)
{
    unsigned value = 2166136261u;
    for (unsigned i = 0; i < length; ++i) value = (value ^ data[i]) * 16777619u;
    return value;
}

unsigned OriginalTestCount() { return sizeof(DibVectors) / sizeof(DibVectors[0]); }
unsigned OriginalSelfTest()
{
    // Separately check every recovered expression against its 8-bit truth table.
    for (unsigned r = 0; r < 256; ++r)
        if ((ApplyRop(r, 0xcccccccc, 0xaaaaaaaa, 0xf0f0f0f0) & 255) != r) return 0x10000 + r;
    unsigned char source[1400], dest[1400];
    for (unsigned i = 0; i < OriginalTestCount(); ++i) {
        const auto &v = DibVectors[i];
        const int depth = v[0], stride = ((19 * depth + 31) / 32) * 4 + 4;
        const unsigned length = 256 + 13 * stride;
        unsigned s = v[2], d = s ^ 0x98765432;
        for (unsigned j = 0; j < length; ++j) {
            s = s * 1664525u + 1013904223u; source[j] = s >> 24;
            d = d * 1664525u + 1013904223u; dest[j] = d >> 24;
        }
        const int offset = 128 + (v[10] ? 12 * stride : 0), delta = v[10] ? -stride : stride;
        Bitmap a = {source + offset, 19, 13, delta, depth}, b = {dest + offset, 19, 13, delta, depth};
        unsigned pattern[64];
        for (unsigned j = 0; j < 64; ++j) pattern[j] = (v[2] * 214013u + 2531011u) ^ (j * 0x1020304u);
        const Brush brush = {v[2] * 214013u + 2531011u, v[13] ? pattern : nullptr};
        const bool result = BitBlt(b, (int)v[3], (int)v[4], (int)v[7], (int)v[8],
                                  v[9] ? &b : &a, (int)v[5], (int)v[6], v[1] << 16, brush);
        if ((unsigned)result != v[11] || Hash(dest, length) != v[12]) return i + 1;
    }
    return 0;
}
#endif
}
