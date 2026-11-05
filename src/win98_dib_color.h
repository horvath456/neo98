// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// DIBENG, 01C7/01EE, 0214 and 0270/02C4.
// The original expansion tables round interior 5/6-bit values upward.
namespace Win98DibColor {
using U = unsigned;
inline U Expand5(U value) {
  value &= 31;
  return !value ? 0 : value == 31 ? 255 : (value + 1) * 8;
}
inline U Expand6(U value) {
  value &= 63;
  return !value ? 0 : value == 63 ? 255 : (value + 1) * 4;
}
inline U Swap(U value) {
  return ((value & 255) << 16) | (value & 0xff00) | ((value >> 16) & 255);
}
template <class E> U ToColor(E &e, U palette, U depth, bool rgb565, U pixel) {
  if (depth <= 8) {
    const U mask = depth == 1 ? 1 : depth == 4 ? 15 : 255;
    return Swap(e.Read32(palette + (pixel & mask) * 4));
  }
  if (depth == 16) {
    const U blue = Expand5(pixel),
            green = rgb565 ? Expand6(pixel >> 5) : Expand5(pixel >> 5);
    const U red = Expand5(pixel >> (rgb565 ? 11 : 10));
    return red | (green << 8) | (blue << 16);
  }
  return Swap(pixel);
}
inline U Quantize(U channel, U bias, U shift) {
  return channel < bias ? 0 : (channel - bias) >> shift;
}
inline U FromColor16(U color, bool rgb565) {
  const U red = Quantize(color & 255, 4, 3),
          green = Quantize((color >> 8) & 255, rgb565 ? 2 : 4, rgb565 ? 2 : 3),
          blue = Quantize((color >> 16) & 255, 4, 3);
  return (red << (rgb565 ? 11 : 10)) | (green << 5) | blue;
}
} // namespace Win98DibColor
