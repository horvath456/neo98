// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dib_public.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct Provider {
  unsigned memory[64], config[4], events[128], used = 0;
  void Event(unsigned k, unsigned a = 0, unsigned b = 0, unsigned c = 0,
             unsigned d = 0, unsigned f = 0, unsigned g = 0, unsigned h = 0) {
    unsigned v[] = {k, a, b, c, d, f, g, h};
    memcpy(events + used, v, sizeof v);
    used += 8;
  }
  unsigned ScratchWord() { return 0x20; }
  unsigned Read32(unsigned p) {
    if (p >= 256 || p & 3)
      abort();
    return memory[p / 4];
  }
  void Write32(unsigned p, unsigned v) {
    if (p >= 256 || p & 3)
      abort();
    memory[p / 4] = v;
  }
  unsigned MapView(unsigned s, unsigned a, unsigned hi, unsigned off,
                   unsigned n) {
    Event(1, s, a, hi, off, n);
    return config[0];
  }
  void UnmapView(unsigned p) { Event(2, p); }
  unsigned CreateSection16(unsigned dc, unsigned info, unsigned usage,
                           unsigned out, unsigned sec, unsigned off,
                           unsigned mode) {
    Event(3, dc, info, usage, out, sec, off, mode);
    if (config[1])
      Write32(out, config[2]);
    return config[1];
  }
  unsigned CreateBitmap16(unsigned dc, unsigned header, unsigned flags,
                          unsigned bits, unsigned info, unsigned usage) {
    Event(4, dc, header, flags, bits, info, usage);
    return config[1];
  }
  unsigned SetDibits(unsigned dc, unsigned bm, unsigned start, unsigned count,
                     unsigned bits, unsigned info, unsigned usage) {
    Event(5, dc, bm, start, count, bits, info, usage);
    return config[3];
  }
};
extern "C" unsigned dib_public_case(unsigned *memory, const unsigned *a,
                                    unsigned *trace) {
  Provider p{};
  memcpy(p.memory, memory, 256);
  memcpy(p.config, a + 7, 16);
  unsigned result =
      a[0] ? Win98DibPublic::CreateBitmap(p, a[1], a[2], a[3], a[4], a[5], a[6])
           : Win98DibPublic::CreateSection(p, a[1], a[2], a[3], a[4], a[5],
                                           a[6]);
  memcpy(memory, p.memory, 256);
  trace[0] = p.used;
  memcpy(trace + 1, p.events, p.used * 4);
  return result;
}
#ifdef DIB_API_PUBLIC_STANDALONE
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  unsigned a[11], before[64], after[64], trace[129], actualtrace[129], expected,
      n = 0;
  while (fread(a, sizeof a, 1, f) == 1) {
    if (fread(before, sizeof before, 1, f) != 1 ||
        fread(after, sizeof after, 1, f) != 1 ||
        fread(trace, sizeof trace, 1, f) != 1 || fread(&expected, 4, 1, f) != 1)
      return 3;
    memset(actualtrace, 0, sizeof actualtrace);
    unsigned value = dib_public_case(before, a, actualtrace);
    if (value != expected || memcmp(before, after, sizeof before) ||
        memcmp(actualtrace, trace, sizeof trace)) {
      fprintf(stderr, "case %u\n", n);
      return 4;
    }
    ++n;
  }
  fclose(f);
  printf("DIB public corpus: %u cases\n", n);
  return 0;
}
#endif
