// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Scan conversion of a closed polygon into the horizontal spans of a region.
// GDI's own scan converter lives in segment 30 and is not ported; the spans
// this emits are the ones the original produces for the shapes seen so far.
// The vertex lists come from the ported original generators: GDI for a
// full ellipse and GDI flags 10h for a round rectangle.
namespace Win98GdiRegionShape {
struct Point { int x, y; };
constexpr unsigned MaxCrossings = 1024;
// Samples every scan line at its pixel centre. ALTERNATE (1) counts parity,
// WINDING (2) the running direction sum. Emit(y, left, right) reports one
// span and answers false to stop. Returns false on a rejected argument or a
// stopped emitter.
template <class Emit>
bool Spans(const Point *p, unsigned count, unsigned mode, Emit emit) {
  if (count < 2 || (mode != 1 && mode != 2))
    return false;
  int top = p[0].y, bottom = p[0].y;
  for (unsigned i = 1; i < count; ++i) {
    if (p[i].y < top) top = p[i].y;
    if (p[i].y > bottom) bottom = p[i].y;
  }
  for (int y = top; y < bottom; ++y) {
    struct Crossing { double x; int direction; } crossings[MaxCrossings];
    unsigned n = 0;
    const double sample = y + 0.5;
    for (unsigned i = 0; i < count; ++i) {
      const Point a = p[i], b = p[(i + 1) % count];
      if (a.y == b.y) continue;
      const int lo = a.y < b.y ? a.y : b.y, hi = a.y < b.y ? b.y : a.y;
      if (sample < lo || sample >= hi) continue;
      const double x = a.x + (sample - a.y) * (double)(b.x - a.x) / (double)(b.y - a.y);
      if (n < MaxCrossings) crossings[n++] = {x, b.y > a.y ? 1 : -1};
    }
    for (unsigned i = 1; i < n; ++i)
      for (unsigned j = i; j > 0 && crossings[j - 1].x > crossings[j].x; --j) {
        const auto c = crossings[j]; crossings[j] = crossings[j - 1]; crossings[j - 1] = c;
      }
    int winding = 0;
    for (unsigned i = 0; i + 1 < n; ++i) {
      winding += mode == 2 ? crossings[i].direction : 1;
      const bool inside = mode == 2 ? winding != 0 : (winding & 1) != 0;
      if (!inside) continue;
      auto up = [](double v) { const int i = (int)v; return (double)i < v ? i + 1 : i; }; // ceil without libm
      const int left = up(crossings[i].x - 0.5), right = up(crossings[i + 1].x - 0.5);
      if (right <= left) continue;
      if (!emit(y, left, right)) return false;
    }
  }
  return true;
}
// The same sampling for several closed polygons at once, the way GDI
// hands CreatePolyPolygonRgn's point runs to one scan conversion :
// every polygon closes on itself, and the crossings of all of them count
// together. `alternate` selects the parity rule, otherwise the winding rule.
template <class Emit>
bool PolySpans(const Point *p, const unsigned *counts, unsigned polygons, bool alternate, Emit emit) {
  unsigned total = 0;
  for (unsigned k = 0; k < polygons; ++k) {
    if (counts[k] < 2) return false;
    total += counts[k];
  }
  if (!total) return false;
  int top = p[0].y, bottom = p[0].y;
  for (unsigned i = 1; i < total; ++i) {
    if (p[i].y < top) top = p[i].y;
    if (p[i].y > bottom) bottom = p[i].y;
  }
  for (int y = top; y < bottom; ++y) {
    struct Crossing { double x; int direction; } crossings[MaxCrossings];
    unsigned n = 0;
    const double sample = y + 0.5;
    for (unsigned k = 0, first = 0; k < polygons; first += counts[k], ++k)
      for (unsigned i = 0; i < counts[k]; ++i) {
        const Point a = p[first + i], b = p[first + (i + 1) % counts[k]];
        if (a.y == b.y) continue;
        const int lo = a.y < b.y ? a.y : b.y, hi = a.y < b.y ? b.y : a.y;
        if (sample < lo || sample >= hi) continue;
        const double x = a.x + (sample - a.y) * (double)(b.x - a.x) / (double)(b.y - a.y);
        if (n < MaxCrossings) crossings[n++] = {x, b.y > a.y ? 1 : -1};
      }
    for (unsigned i = 1; i < n; ++i)
      for (unsigned j = i; j > 0 && crossings[j - 1].x > crossings[j].x; --j) {
        const auto c = crossings[j]; crossings[j] = crossings[j - 1]; crossings[j - 1] = c;
      }
    int winding = 0;
    for (unsigned i = 0; i + 1 < n; ++i) {
      winding += alternate ? 1 : crossings[i].direction;
      const bool inside = alternate ? (winding & 1) != 0 : winding != 0;
      if (!inside) continue;
      auto up = [](double v) { const int i = (int)v; return (double)i < v ? i + 1 : i; };
      const int left = up(crossings[i].x - 0.5), right = up(crossings[i + 1].x - 0.5);
      if (right <= left) continue;
      if (!emit(y, left, right)) return false;
    }
  }
  return true;
}
// A ring from the ellipse and round-rectangle kernels is convex and lists the
// pixels of the outline itself, not a mathematical polygon. Its region is
// therefore the closed run between the leftmost and the rightmost point of
// every scan line the ring touches, including the first and the last one.
// Fills minimum[y - top] and maximum[y - top] for every row; a row the ring
// misses keeps minimum > maximum.
inline void ConvexRows(const Point *p, unsigned count, int top, int bottom,
                       int *minimum, int *maximum) {
  for (int y = top; y <= bottom; ++y) {
    minimum[y - top] = 1;
    maximum[y - top] = 0;
  }
  auto touch = [&](int y, int x) {
    if (y < top || y > bottom) return;
    int &lo = minimum[y - top], &hi = maximum[y - top];
    if (lo > hi) { lo = x; hi = x; return; }
    if (x < lo) lo = x;
    if (x > hi) hi = x;
  };
  for (unsigned i = 0; i < count; ++i) {
    const Point a = p[i], b = p[(i + 1) % count];
    touch(a.y, a.x);
    if (a.y == b.y) { touch(b.y, b.x); continue; }
    const int step = a.y < b.y ? 1 : -1;
    for (int y = a.y + step; y != b.y; y += step) {
      const double x = a.x + (double)(y - a.y) * (b.x - a.x) / (b.y - a.y);
      const int i = (int)x;
      touch(y, (double)i < x ? i + 1 : i); // ceil without libm
      touch(y, (int)x);
    }
  }
}
} // namespace Win98GdiRegionShape
