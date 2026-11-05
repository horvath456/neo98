// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/pe32.h"
#include "../src/win_image.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *file = fopen(argv[1], "rb");
  assert(file);
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);
  std::vector<unsigned char> bytes(size);
  assert(fread(bytes.data(), 1, size, file) == (unsigned)size);
  fclose(file);
  Pe32::Image pe;
  assert(pe.Parse(bytes.data(), bytes.size()));
  unsigned icons = 0, truncations = 0;
  for (unsigned id = 1; id < 256; ++id) {
    unsigned count = 0;
    const auto *resource = pe.Resource(3, id, &count);
    if (!resource)
      continue;
    WinImage::Image image;
    assert(image.DecodeIcon(resource, count));
    assert(image.width == 16 || image.width == 32);
    ++icons;
    for (unsigned n = 0; n < count; ++n) {
      WinImage::Image broken;
      assert(!broken.DecodeIcon(resource, n));
      ++truncations;
    }
    WinImage::Image copy;
    assert(copy.Copy(image, 48, 48));
    assert(copy.width == 48 && copy.height == 48);
    for (unsigned y = 0; y < 48; ++y)
      for (unsigned x = 0; x < 48; ++x) {
        unsigned src =
            y * image.height / 48 * image.width + x * image.width / 48;
        assert(copy.color[y * 48 + x] == image.color[src] &&
               copy.mask[y * 48 + x] == image.mask[src]);
      }
  }
  assert(icons == 11);
  // Four monochrome pixels: black, white, transparent and destination
  // inversion.
  unsigned char dib[56] = {40, 0, 0, 0, 4, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0};
  dib[44] = dib[45] = dib[46] = 255;
  dib[48] = 0x50;
  dib[52] = 0x30;
  WinImage::Image image;
  assert(image.DecodeIcon(dib, sizeof dib));
  unsigned pixels[12];
  for (auto &p : pixels)
    p = 0x123456;
  auto dc = Gdi::CreateSurfaceDC({(unsigned char *)pixels, 6, 2, 24, 32});
  assert(dc);
  assert(image.Draw(dc, 1, 0));
  assert(pixels[0] == 0x123456 && pixels[1] == 0 && pixels[2] == 0xffffff &&
         pixels[3] == 0x123456 && pixels[4] == 0xedcba9 &&
         pixels[5] == 0x123456);
  Gdi::DeleteDC(dc);
  assert(!Gdi::LiveObjects());
  // Reject palette references past biClrUsed and incomplete masks.
  dib[32] = 1;
  assert(!image.DecodeIcon(dib, 48));
  dib[32] = 0;
  assert(!image.DecodeIcon(dib, sizeof dib, 257, 16));
  printf("IMAGE RESOURCES PASS icons=%u truncations=%u "
         "monochrome-inversion=PASS scaling=PASS\n",
         icons, truncations);
}
