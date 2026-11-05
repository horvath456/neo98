// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_layout.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct Memory {
  unsigned char *data;
  unsigned Read8(unsigned p) { return data[p & 65535]; }
  unsigned Read16(unsigned p) { return Read8(p) | (Read8(p + 1) << 8); }
  unsigned Read32(unsigned p) { return Read16(p) | (Read16(p + 2) << 16); }
  void Write8(unsigned p, unsigned v) { data[p & 65535] = v; }
  void Write16(unsigned p, unsigned v) {
    Write8(p, v);
    Write8(p + 1, v >> 8);
  }
  void Write32(unsigned p, unsigned v) {
    Write16(p, v);
    Write16(p + 2, v >> 16);
  }
  unsigned ProbeBits(unsigned p, unsigned n, bool write) {
    Write32(0x700, p);
    Write32(0x704, n);
    Write32(0x708, write);
    return Read32(0x70c);
  }
};
extern "C" unsigned dib_api_case(unsigned char *memory, const unsigned *a) {
  Memory e{memory};
  switch (a[0]) {
  case 0:
    return Win98Dib::CreateDevice(e, a[1], a[2], a[3], a[4]);
  case 1:
    return Win98Dib::Validate(e, a[1], a[2]);
  case 2:
    Win98Dib::Construct(e, a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8],
                        a[9]);
    return 0;
  case 3:
    return Win98Dib::TransferDevice(e, a[1], a[2], a[3], a[4], a[5]);
  case 4:
    Win98Dib::ExpandCore(e, a[1], a[2], a[3], a[4]);
    return 0;
  case 5:
    return Win98Dib::ProbeBits(e, a[1], a[2], a[3], a[4], a[5]);
  }
  std::abort();
}
#ifdef DIB_API_STANDALONE
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  unsigned a[10], expected;
  unsigned char before[65536], after[65536];
  unsigned count = 0;
  while (fread(a, sizeof a, 1, f) == 1) {
    for (unsigned i = 0; i < 65536; ++i)
      before[i] = (i * 29 + 71) & 255;
    if (fread(&expected, 4, 1, f) != 1 ||
        fread(before + 0x100, 0x700, 1, f) != 1)
      return 3;
    memcpy(after, before, sizeof before);
    if (fread(after + 0x100, 0x700, 1, f) != 1 ||
        fread(after + 0x1000, 0x440, 1, f) != 1)
      return 3;
    const unsigned actual = dib_api_case(before, a);
    if (actual != expected || memcmp(before, after, sizeof before)) {
      fprintf(stderr, "case %u\n", count);
      return 4;
    }
    ++count;
  }
  fclose(f);
  printf("DIB core ASan corpus: %u cases\n", count);
  return 0;
}
#endif
