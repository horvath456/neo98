// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_color.h"
#include <cstdio>
struct Palette {
  unsigned Read32(unsigned p) { return p * 0x010101u; }
};
extern "C" unsigned dib_color(unsigned operation, unsigned value) {
  if (operation < 2) {
    Palette e;
    return Win98DibColor::ToColor(e, 0, 16, operation, value);
  }
  return Win98DibColor::FromColor16(value, operation == 3);
}
#ifdef DIB_API_COLOR_STANDALONE
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  unsigned a[3], count = 0;
  while (fread(a, sizeof a, 1, f) == 1) {
    if (dib_color(a[0], a[1]) != a[2])
      return 3;
    ++count;
  }
  fclose(f);
  printf("DIB color ASan corpus: %u cases\n", count);
  return 0;
}
#endif
