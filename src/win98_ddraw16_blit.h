// SPDX-License-Identifier: GPL-2.0-or-later
// DirectDraw HEL blits through the DIB engine, catalog entry 41. Each
// source/destination argument block contains width, height, x, y, depth,
// stride and a linear pixel address. Rectangles supply the clipping bounds.
// Use BitBlt when dimensions match and StretchBlt otherwise, both with
// SRCCOPY. Equal pixel addresses identify a blit within one surface.
// Windows 98 uses a flat selector to address these buffers. Neo98 already
// accesses linear guest addresses, so it only needs to validate the supplied
// memory. The caller restricts this path to compatible 8-, 16- and 32-bit
// surfaces and falls back to GDI otherwise.
#pragma once
namespace Win98DDraw16Blit
{
using U = unsigned;
// Return this value when no flat selector is available.
enum : U { Refused = 0xffffu, Done = 0u };
// An argument-list block describing a DIBENGINE surface.
struct Plane
{
    int width = 0, height = 0, x = 0, y = 0;
    U depth = 0, stride = 0, bits = 0;
};
// Sign-extend the scanline stride from its source word.
inline int DeltaScan(U stride) { return int(short(stride & 0xffffu)); }
// No flat selector is required here; validate the pixel addresses directly.
inline bool Addressable(const Plane &p)
{
    return p.bits && p.stride && p.width > 0 && p.height > 0 &&
           (p.depth == 8 || p.depth == 16 || p.depth == 24 || p.depth == 32);
}
// Equal extents use DIBENG.1; other sizes use DIBENG.27.
inline bool Stretches(const Plane &source, const Plane &destination)
{
    return source.width != destination.width || source.height != destination.height;
}
// Equal pixel addresses mean a blit within one surface.
inline bool SameSurface(const Plane &source, const Plane &destination)
{
    return source.bits == destination.bits;
}
// Both blit variants use this raster operation.
enum : U { SrcCopy = 0x00cc0020u };
}
