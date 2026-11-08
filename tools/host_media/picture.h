// SPDX-License-Identifier: GPL-2.0-or-later
// Decoding of the catalogue's pictures (JPEG, PNG, WebP as far as the host
// can), with the host's own image library: picture_linux.cpp (stb_image and GdkPixbuf),
// picture_macos.mm (ImageIO).
#pragma once
#include <cstddef>
#include <vector>
namespace HostMediaPicture
{
// 0xAARRGGBB pixels with premultiplied colour, top-down.
bool Decode(const unsigned char *data, size_t size, std::vector<unsigned> &pixels, unsigned &width, unsigned &height);
}
