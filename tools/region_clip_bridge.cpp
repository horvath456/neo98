/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_region_clip.h"
#include <cstdio>
#include <cstring>
extern "C" void region_delta(const unsigned *a, unsigned *out) {
  using namespace Win98DcMap;
  State s;
  s.window_origin = {S(a[0]), S(a[1])};
  s.window_extent = {S(a[2]), S(a[3])};
  s.viewport_effective = {S(a[4]), S(a[5])};
  s.viewport_extent = {S(a[6]), S(a[7])};
  s.viewport_origin = {S(a[8]), S(a[9])};
  s.device_origin = {S(a[10]), S(a[11])};
  s.translation = {S(a[12]), S(a[13])};
  s.shifts = {S(a[14]), S(a[15])};
  s.mode = a[16];
  s.flags = a[17];
  s.transform_flags = a[18];
  auto r = Win98RegionClip::OffsetDelta(s, a[19], a[20]);
  out[0] = unsigned(r.x) & 65535;
  out[1] = unsigned(r.y) & 65535;
}
#ifdef REGION_CLIP_STANDALONE
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 3;
  unsigned record[23], out[2], n = 0;
  while (fread(record, 4, 23, f) == 23) {
    region_delta(record, out);
    if (memcmp(out, record + 21, 8))
      return 4;
    ++n;
  }
  fclose(f);
  printf("Region logical-offset corpus PASS: %u\n", n);
}
#endif
