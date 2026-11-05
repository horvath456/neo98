// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gdi.h"
// New resource adapter. Icon AND/XOR masks retain monochrome inversion.
namespace WinImage {
class Image {
public:
  unsigned width = 0, height = 0, failure = 0;
  bool monochrome = false;
  static void*operator new(__SIZE_TYPE__) noexcept;
  static void operator delete(void*) noexcept;
  unsigned *color = nullptr, *mask = nullptr;
  Image() = default;
  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  ~Image();
  bool DecodeIcon(const unsigned char *, unsigned bytes, unsigned cx = 0,
                  unsigned cy = 0);
  bool Copy(const Image &, unsigned cx = 0, unsigned cy = 0);
  bool Draw(Gdi::HDC, int x, int y, unsigned flags = 3) const;
};
} // namespace WinImage
