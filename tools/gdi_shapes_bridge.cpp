// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_shapes.h"
using namespace Win98GdiShapes;
extern "C" unsigned shapes_run(unsigned op, const int *input, unsigned count,
                               int *output) {
  Point points[2048]{};
  Edge edges[2048]{};
  unsigned n = 0;
  if (op == 0 || op == 1) {
    n = op == 0 ? Quadrant(points, 2048, input[0], input[1])
                : EllipseVertices(points, 2048,
                                  {input[0], input[1], input[2], input[3]});
    for (unsigned i = 0; i < n; ++i) {
      output[i * 2] = points[i].x;
      output[i * 2 + 1] = points[i].y;
    }
    return n * 2;
  }
  if (op == 3) {
    struct Provider {
      int *out;
      unsigned used = 0;
      void Fill(Rect r) {
        out[used++] = 1;
        out[used++] = r.left;
        out[used++] = r.top;
        out[used++] = r.right;
        out[used++] = r.bottom;
      }
      void Outline(const Point *p, U n) {
        out[used++] = 2;
        for (U i = 0; i < n; ++i) {
          out[used++] = p[i].x;
          out[used++] = p[i].y;
        }
      }
    } e{output};
    Rectangle(e, {input[0], input[1], input[2], input[3]}, input[4], input[5]);
    return e.used;
  }
  const unsigned contours = input[0], mode = input[1];
  unsigned total = 0;
  for (unsigned i = 0; i < contours; ++i)
    total += input[2 + i];
  for (unsigned i = 0; i < total; ++i)
    points[i] = {input[2 + contours + i * 2], input[3 + contours + i * 2]};
  unsigned base = 0;
  int bottom = -32768;
  for (unsigned i = 0; i < total; ++i)
    if (points[i].y > bottom)
      bottom = points[i].y;
  for (unsigned i = 0; i < contours; ++i) {
    n += BuildEdges(edges + n, points, base, input[2 + i]);
    base += input[2 + i];
  }
  struct Provider {
    int *out;
    unsigned used = 0;
    bool Span(int y, int l, int r) {
      if (used + 3 > 65536)
        return false;
      out[used++] = y;
      out[used++] = l;
      out[used++] = r;
      return true;
    }
  } provider{output};
  if (!PolygonSpans(provider, edges, n, points, bottom, mode))
    return ~0u;
  return provider.used;
}
