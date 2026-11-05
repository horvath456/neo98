// SPDX-License-Identifier: GPL-2.0-or-later
#include "raster.h"
#include "dibeng.h"

void FillRect(Surface &s, Rect r, unsigned color)
{
    const Rect bounds = {0, 0, s.width, s.height};
    if (!Win98::IntersectRect(&r, &r, &bounds)) return;
    DibEng::Bitmap bitmap = {reinterpret_cast<unsigned char *>(s.pixels), s.width, s.height, s.stride * 4, 32};
    DibEng::BitBlt(bitmap, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   nullptr, 0, 0, 0x00f00021, {color});
}

void DrawEdge(Surface &s, Rect r, bool pressed)
{
    const unsigned light = pressed ? 0xff404040 : 0xffffffff;
    const unsigned dark = pressed ? 0xffffffff : 0xff404040;
    FillRect(s, {r.left, r.top, r.right, r.top + 2}, light);
    FillRect(s, {r.left, r.top, r.left + 2, r.bottom}, light);
    FillRect(s, {r.left, r.bottom - 2, r.right, r.bottom}, dark);
    FillRect(s, {r.right - 2, r.top, r.right, r.bottom}, dark);
}

bool RasterSelfTest()
{
    // Guard words and row padding catch writes beyond clipped bounds.
    unsigned memory[22];
    for (unsigned &p : memory) p = 0x12345678;
    Surface s = {memory + 1, 3, 4, 5};
    FillRect(s, {-100, -100, 2, 2}, 0xaabbccdd);
    FillRect(s, {2, 2, 999, 999}, 0x55667788);
    FillRect(s, {10, 10, 20, 20}, 0);
    FillRect(s, {2, 2, 1, 1}, 0);
    FillRect(s, {-20, -20, -1, -1}, 0);
    if (memory[0] != 0x12345678 || memory[21] != 0x12345678) return false;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 5; ++x) {
            const unsigned expected = x < 2 && y < 2 ? 0xaabbccdd :
                                      x == 2 && y >= 2 ? 0x55667788 : 0x12345678;
            if (s.pixels[y * s.stride + x] != expected) return false;
        }
    }
    return true;
}
