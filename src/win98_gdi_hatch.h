// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The six hatch masks of the display driver. DIBENG realizes a hatch brush
// like a monochrome pattern brush: shifts the hatch index left by three
// and reads eight mask bytes from the table, rotates them to
// the brush origin through the same helper as the monochrome DDB path, and expands them to one byte per pixel.
namespace Win98GdiHatch {
using U = unsigned;
constexpr U Styles = 6;
// HS_HORIZONTAL, HS_VERTICAL, HS_FDIAGONAL, HS_BDIAGONAL, HS_CROSS,
// HS_DIAGCROSS. Bit 7 of a row byte is its leftmost pixel.
inline const unsigned char Masks[Styles][8] = {
    {0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08},
    {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01},
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},
    {0x08, 0x08, 0x08, 0x08, 0xff, 0x08, 0x08, 0x08},
    {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81},
};
// One bit per pixel of the tile, row major, in the order the expansion at
// Windows 98 walks it. A set bit is a hatch pixel.
inline bool Pixel(U style, U x, U y) {
  return style < Styles && (Masks[style][y & 7] >> (7 - (x & 7))) & 1;
}
} // namespace Win98GdiHatch
