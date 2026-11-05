#include "win98_gdi_palette_tables.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Solid-brush dithering for indexed displays. First try an exact fixed color;
// otherwise dither when the device flags permit it or use the nearest color.
// Build an 8x8 pattern from at most four VGA cube colors, allocating pixels
// according to channel magnitudes and the ordered threshold table.
namespace Win98DibDither {
using U = unsigned;
// Map sorted channel levels and intensity bits to VGA color indices.
constexpr unsigned char Order[128] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x00,0x02,0x01,0x03,0x04,0x06,0x05,0x07,0x08,0x0a,0x09,0x0b,0x0c,0x0e,0x0d,0x0f,
    0x00,0x01,0x04,0x05,0x02,0x03,0x06,0x07,0x08,0x09,0x0c,0x0d,0x0a,0x0b,0x0e,0x0f,
    0x00,0x04,0x01,0x05,0x02,0x06,0x03,0x07,0x08,0x0c,0x09,0x0d,0x0a,0x0e,0x0b,0x0f,
    0x00,0x04,0x02,0x06,0x01,0x05,0x03,0x07,0x08,0x0c,0x0a,0x0e,0x09,0x0d,0x0b,0x0f,
    0x00,0x02,0x04,0x06,0x01,0x03,0x05,0x07,0x08,0x0a,0x0c,0x0e,0x09,0x0b,0x0d,0x0f,
    0x00,0x04,0x01,0x05,0x02,0x06,0x03,0x07,0x08,0x0c,0x09,0x0d,0x0a,0x0e,0x0b,0x0f,
    0x00,0x01,0x04,0x05,0x02,0x03,0x06,0x07,0x08,0x09,0x0c,0x0d,0x0a,0x0b,0x0e,0x0f};
// Allocation order for the 64 pixels in the dither pattern.
constexpr unsigned char Positions[64] = {
    0x00,0x24,0x04,0x20,0x12,0x36,0x16,0x32,0x02,0x26,0x06,0x22,0x10,0x34,0x14,0x30,
    0x09,0x2d,0x0d,0x29,0x1b,0x3f,0x1f,0x3b,0x0b,0x2f,0x0f,0x2b,0x19,0x3d,0x1d,0x39,
    0x01,0x25,0x05,0x21,0x13,0x37,0x17,0x33,0x03,0x27,0x07,0x23,0x11,0x35,0x15,0x31,
    0x08,0x2c,0x0c,0x28,0x1a,0x3e,0x1e,0x3a,0x0a,0x2e,0x0e,0x2a,0x18,0x3c,0x1c,0x38};
// Map VGA colors to fixed system-palette slots.
constexpr unsigned char Slot[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0xf8,0x00,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff};
// Find an exact fixed system color and its palette slot.
inline bool StaticSlot(U color, U &slot)
{
    for (U i = 0; i < 20; ++i)
        if ((color & 0xffffff) == Win98GdiPalette::DefaultColors[i]) {
            slot = i < 10 ? i : 246 + i - 10; return true;
        }
    return false;
}
// Build a 64-entry, row-major dither pattern for a COLORREF.
inline void Pattern(U color, unsigned char (&out)[64])
{
    U max = color & 255, mid = (color >> 8) & 255, min = (color >> 16) & 255, order = 0;
    if (min > max) { const U t = min; min = max; max = t; order = 4; }
    if (min > mid) { const U t = min; min = mid; mid = t; order |= 2; }
    if (mid > max) { const U t = mid; mid = max; max = t; order |= 1; }
    const unsigned char *code = Order + order * 16;
    const int a = int((max + 1) >> 2), b = int((mid + 1) >> 2), c = int((min + 1) >> 2);
    struct Run { int count; U color; } runs[4];
    U n = 0;
    auto add = [&](int count, U k) { if (count) runs[n++] = {count, code[k]}; };
    if (mid + max > 256) {
        if (min + max > 256) {                         //
            const int first = 2 * (64 - a), cx = b - c, si = c + a - 64;
            add(first, 7);
            add(64 - si - cx - first, 9);
            add(cx, 11);
            add(si, 15);
        } else {                                       //
            const int cx = 2 * c, si = a - b, top = b + a - 64;
            add(64 - top - si - cx, 3);
            add(cx, 7);
            add(si, 9);
            add(top, 11);
        }
    } else if (max > 0x80) {                           //
        const int first = 2 * (64 - b - a), cx = 2 * c, si = 2 * (a - 32);
        add(first, 1);
        add(64 - si - cx - first, 3);
        add(cx, 7);
        add(si, 9);
    } else {                                           //
        const int di = 2 * (32 - a), ax = 2 * (a - b), cx = 2 * c;
        add(di, 0);
        add(ax, 1);
        add(64 - cx - ax - di, 3);
        add(cx, 7);
    }
    if (n <= 2) {                                      //
        if (n < 2) { for (auto &p : out) p = Slot[runs[0].color & 15]; return; }
        const bool second = runs[1].count > runs[0].count;
        const Run &fill = second ? runs[1] : runs[0], &rest = second ? runs[0] : runs[1];
        for (auto &p : out) p = Slot[fill.color & 15];
        const int start = second ? 0 : runs[0].count;
        for (int i = 0; i < rest.count; ++i) out[Positions[(start + i) & 63]] = Slot[rest.color & 15];
        return;
    }
    // Fill with the most frequent color first, then skip its assigned slots.
    U big = n == 3 ? (runs[1].count < runs[2].count ? 2 : 1) : (runs[2].count < runs[3].count ? 3 : 2);
    if (runs[1].count > runs[big].count) big = 1;
    if (runs[0].count > runs[big].count) big = 0;
    for (auto &p : out) p = Slot[runs[big].color & 15];
    int at = 0;
    for (U i = 0; i < n; ++i) {
        if (i != big)
            for (int k = 0; k < runs[i].count; ++k) out[Positions[(at + k) & 63]] = Slot[runs[i].color & 15];
        at += runs[i].count;
    }
}
}
