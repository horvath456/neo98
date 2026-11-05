// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Portable equivalent of DIBENG's generated SHRD/SAR pixel conversions.
// Original DIB_DIBBLT -> setup3:30EC -> generator5:0000 emits these
// for direct-color pixels. They differ from the ColorInfo lookup tables.
namespace Win98DibRaster {
using U = unsigned;
inline U Expand(U value, unsigned bits) {
  const U mask = (1u << bits) - 1;
  value &= mask;
  return (value << (8 - bits)) | ((value & 1) ? ((1u << (8 - bits)) - 1) : 0);
}
inline U To32(U value, U depth, bool rgb565) {
  if (depth == 24)
    return value & 0xffffff;
  if (depth != 16)
    return value;
  const U b = Expand(value, 5), g = Expand(value >> 5, rgb565 ? 6 : 5),
          r = Expand(value >> (rgb565 ? 11 : 10), 5);
  return b | (g << 8) | (r << 16) | (rgb565 ? 0 : (value & 0x8000) << 9);
}
// The alternating 24-bit loads retain one byte from the previous EAX.
inline U Load24(U previous, U value, U pixel) {
  return (value & 0xffffff) |
         (((previous >> ((pixel & 1) ? 16 : 8)) & 255) << 24);
}
// The generated monochrome path averages red/blue first, then green.
// For RGB565 the first word of each DWORD keeps six green bits; the
// second generated word conversion masks to five. Both use threshold15.
inline U Monochrome(U value, U depth, bool rgb565, U pixel) {
  const U blue = depth == 16 ? value & 31 : value & 255;
  const U red =
      depth == 16 ? (value >> (rgb565 ? 11 : 10)) & 31 : (value >> 16) & 255;
  const U green = depth == 16
                      ? (value >> 5) & (rgb565 && !(pixel & 1) ? 63 : 31)
                      : (value >> 8) & 255;
  return (((blue + red) / 2 + green) / 2) >= (depth == 16 ? 15u : 127u);
}
inline U Convert(U value, U sourceDepth, bool source565, U targetDepth,
                 bool target565) {
  if (sourceDepth == targetDepth && source565 == target565)
    return value;
  value = To32(value, sourceDepth, source565);
  if (targetDepth == 16) {
    if (target565)
      return ((value >> 3) & 31) | ((value >> 5) & 0x7e0) |
             ((value >> 8) & 0xf800);
    return ((value >> 3) & 31) | ((value >> 6) & 0x3e0) |
           ((value >> 9) & 0xfc00);
  }
  return targetDepth == 24 ? value & 0xffffff : value;
}
} // namespace Win98DibRaster
