// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_arcs.h"
extern "C" unsigned arcs_run(unsigned mode, const int *a, int *out) {
  using namespace Win98GdiArcs;
  Point p[2048]{};
  unsigned n =
      mode == 16 ? RoundVertices(p, 2048, {a[0], a[1], a[2], a[3]}, a[4], a[5])
                 : ArcVertices(p, 2048, {a[0], a[1], a[2], a[3]}, {a[4], a[5]},
                               {a[6], a[7]}, mode);
  for (unsigned i = 0; i < n; ++i) {
    out[i * 2] = p[i].x;
    out[i * 2 + 1] = p[i].y;
  }
  return n * 2;
}
