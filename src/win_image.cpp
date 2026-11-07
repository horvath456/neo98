// SPDX-License-Identifier: GPL-2.0-or-later
#include "win_image.h"
#include <stdlib.h>
namespace WinImage {
void*Image::operator new(__SIZE_TYPE__ n) noexcept{return malloc(n);}
void Image::operator delete(void*p) noexcept{free(p);}
Image::~Image(){free(color);free(mask);}
namespace {
unsigned Word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
unsigned Long(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }
} // namespace
bool Image::DecodeIcon(const unsigned char *p, unsigned size, unsigned cx,
                       unsigned cy) {
  failure=13;
  if(!p||size<12)return false;
  const bool core=Long(p)==12;const unsigned header=core?12:40,palette_entry=core?3:4;
  if((!core&&(Long(p)!=40||size<40||Long(p+16)))||Word(p+(core?8:12))!=1)return false;
  unsigned w=core?Word(p+4):Long(p+4),h=core?Word(p+6):Long(p+8),bpp=Word(p+(core?10:14)),colors=core?0:Long(p+32);
  if (!w || w > 256 || !h || h > 512 || (h & 1) ||
      (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32))
    return false;
  h /= 2;
  if (!cx)
    cx = w;
  if (!cy)
    cy = h;
  if (cx > 256 || cy > 256)
    return false;
  if (bpp <= 8) {
    if (!colors)
      colors = 1u << bpp;
    if (colors > 1u << bpp)
      return false;
  } else if (colors)
    return false;
  unsigned pitch = ((w * bpp + 31) / 32) * 4, mask_pitch = ((w + 31) / 32) * 4,
           offset = header + colors * palette_entry;
  if (offset > size || h * pitch > size - offset ||
      h * mask_pitch > size - offset - h * pitch)
    return false;
  auto *pixels=static_cast<unsigned*>(malloc(cx*cy*sizeof(unsigned))),*and_mask=static_cast<unsigned*>(malloc(cx*cy*sizeof(unsigned)));
  if(!pixels||!and_mask){free(pixels);free(and_mask);failure=8;return false;}
  for (unsigned y = 0; y < cy; ++y)
    for (unsigned x = 0; x < cx; ++x) {
      unsigned sx = x * w / cx, sy = h - 1 - y * h / cy;
      const auto *row = p + offset + sy * pitch;
      unsigned value = 0;
      if (bpp <= 8) {
        unsigned bit = sx * bpp, index = (row[bit / 8] >> (8 - bpp - bit % 8)) &
                                         ((1u << bpp) - 1);
        if (index >= colors) {
          free(pixels);
          free(and_mask);
          return false;
        }
        const auto *entry=p+header+index*palette_entry;value=entry[0]|unsigned(entry[1])<<8|unsigned(entry[2])<<16;
      } else {
        row += sx * (bpp / 8);
        value = row[0] | (unsigned)row[1] << 8 | (unsigned)row[2] << 16;
      }
      pixels[y * cx + x] = value;
      and_mask[y * cx + x] =
          (p[offset + h * pitch + sy * mask_pitch + sx / 8] & (128 >> (sx & 7)))
              ? 0xffffffffu
              : 0;
    }
  free(color);
  free(mask);
  color = pixels;
  mask = and_mask;
  width = cx;
  height = cy;
  monochrome = bpp == 1;
  failure=0;
  return true;
}
bool Image::Copy(const Image &other, unsigned cx, unsigned cy) {
  failure=13;
  if (!other.color || !other.mask)
    return false;
  if (!cx)
    cx = other.width;
  if (!cy)
    cy = other.height;
  if (!cx || !cy || cx > 256 || cy > 256)
    return false;
  auto *pixels=static_cast<unsigned*>(malloc(cx*cy*sizeof(unsigned))),*and_mask=static_cast<unsigned*>(malloc(cx*cy*sizeof(unsigned)));
  if(!pixels||!and_mask){free(pixels);free(and_mask);failure=8;return false;}
  for (unsigned y = 0; y < cy; ++y)
    for (unsigned x = 0; x < cx; ++x) {
      unsigned i = y * other.height / cy * other.width + x * other.width / cx;
      pixels[y * cx + x] = other.color[i];
      and_mask[y * cx + x] = other.mask[i];
    }
  free(color);
  free(mask);
  color = pixels;
  mask = and_mask;
  width = cx;
  height = cy;
  monochrome = other.monochrome;
  failure=0;
  return true;
}
bool Image::Draw(Gdi::HDC dc, int x, int y, unsigned flags) const {
  if (!color || !mask || !(flags & 3))
    return false;
  bool result = true;
  if (flags & 1) {
    auto source = Gdi::CreateSurfaceDC(
        {(unsigned char *)mask, (int)width, (int)height, (int)width * 4, 32});
    if (!source)
      return false;
    result = Gdi::BitBlt(dc, x, y, width, height, source, 0, 0,
                         (flags & 2) ? Gdi::SRCAND : Gdi::SRCCOPY);
    Gdi::DeleteDC(source);
  }
  if (flags & 2) {
    auto source = Gdi::CreateSurfaceDC(
        {(unsigned char *)color, (int)width, (int)height, (int)width * 4, 32});
    if (!source)
      return false;
    result = Gdi::BitBlt(dc, x, y, width, height, source, 0, 0,
                         (flags & 1) ? Gdi::SRCINVERT : Gdi::SRCCOPY) &&
             result;
    Gdi::DeleteDC(source);
  }
  return result;
}
} // namespace WinImage
