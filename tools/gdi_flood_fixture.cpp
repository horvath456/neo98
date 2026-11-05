/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../src/win98_gdi_flood.h"

struct Surface {
  unsigned char pixels[7 * 7];
  unsigned char visited[7 * 7];
  unsigned writes;
  bool unchanged;
  bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < 7 && y < 7; }
  unsigned Read(int x, int y) const { return pixels[y * 7 + x]; }
  bool Seen(int x, int y) const { return visited[y * 7 + x]; }
  void Mark(int x, int y) { visited[y * 7 + x] = 1; }
  void Write(int x, int y) {
    ++writes;
    // Exercise a ROP which leaves the destination unchanged. Traversal must
    // still terminate without using the resulting pixel as its visited bit.
    if (!unchanged)
      pixels[y * 7 + x] = ((x ^ y) & 1) ? 2 : 3;
  }
};

int main() {
  Surface border{};
  memset(border.pixels, 1, sizeof border.pixels);
  for (int i = 0; i < 7; ++i)
    border.pixels[i] = border.pixels[6 * 7 + i] =
        border.pixels[i * 7] = border.pixels[i * 7 + 6] = 9;
  border.unchanged = true;
  bool exhausted = true;
  assert(Win98GdiFlood::Fill(border, 3, 3, 9, false, &exhausted));
  assert(!exhausted && border.writes == 25);

  Surface patterned{};
  memset(patterned.pixels, 4, sizeof patterned.pixels);
  assert(Win98GdiFlood::Fill(patterned, 3, 3, 4, true));
  assert(patterned.writes == 49);
  for (unsigned i = 0; i < sizeof patterned.visited; ++i)
    assert(patterned.visited[i]);

  // A lower run can widen around a barrier and reach a second run on the line
  // it came from. Scanning only the two widened endpoints misses this shape.
  Surface concave{};
  memset(concave.pixels, 9, sizeof concave.pixels);
  concave.pixels[1 * 7 + 1] = concave.pixels[1 * 7 + 2] = 1;
  concave.pixels[1 * 7 + 4] = 1;
  for (int x = 1; x <= 5; ++x)
    concave.pixels[2 * 7 + x] = 1;
  assert(Win98GdiFlood::Fill(concave, 4, 1, 1, true));
  assert(concave.visited[1 * 7 + 1] && concave.visited[1 * 7 + 2]);
  assert(concave.writes == 8);
  return 0;
}
