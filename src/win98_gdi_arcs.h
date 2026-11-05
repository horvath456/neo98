// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_gdi_shapes.h"
namespace Win98GdiArcs {
using namespace Win98GdiShapes;
inline I64 Signed32(U value) {
  return I64(value & 0x7fffffff) - I64(value & 0x80000000);
}
// GDI, signed double-word cross-product classification.
inline bool Above(Point p, Point center, Point ray) {
  U first = U(Word(U(p.x - center.x)) * ray.y),
    second = U(Word(U(center.y - p.y)) * ray.x);
  return Signed32(second - first) > 0;
}
// GDI. Original tie goes to the saved endpoint candidate.
inline U Distance(Point p, Point center, Point ray) {
  int x = Word(U(ray.x - p.x + center.x)), y = Word(U(p.y - center.y + ray.y));
  return U(x * x) + U(y * y);
}
inline bool PreferSaved(Point current, Point saved, Point center, Point ray) {
  return Signed32(Distance(current, center, ray)) >=
         Signed32(Distance(saved, center, ray));
}
// GDI: coarse circular search, then signed-WORD binary steps.
inline int Locate(const Point *p, int count, Point center, Point ray) {
  int step = 1;
  while (step < count)
    step *= 2;
  step >>= 3;
  if (!step)
    step = 1;
  int at = 0;
  bool before = Above(p[0], center, ray);
  int next;
  for (;;) {
    next = at + step;
    if (next >= count)
      next -= count;
    bool value = Above(p[next], center, ray);
    if (!before && value)
      break;
    if (next <= at)
      return -1;
    before = value;
    at = next;
  }
  int answer = next;
  step >>= 1;
  if (!step)
    return answer;
  while (step > 0) {
    if (Above(p[at], center, ray)) {
      at -= step;
      if (at < 0)
        at += count;
    } else {
      at += step;
      if (at >= count)
        at -= count;
    }
    step >>= 1;
  }
  if (Above(p[at], center, ray))
    return at;
  return at + 1 < count ? at + 1 : at + 1 - count;
}
inline int SignDifference(int a, int b) { return a == b ? 0 : a > b ? 1 : -1; }
inline Point Step(Point a, Point step, int direction) {
  return {Word(U(a.x + step.x * direction)), Word(U(a.y + step.y * direction))};
}
// GDI arc/Pie/Chord path. The existing original full ellipse
// kernel is shared; arc truncation, endpoint insertion/refinement and ties are
// new.
inline U ArcVertices(Point *out, U capacity, Rect r, Point start, Point end,
                     U flags) {
  U n = EllipseVertices(out, capacity, r);
  if (!n)
    return 0;
  Point center{Word(U(r.left + r.right)) >> 1, Word(U(r.top + r.bottom)) >> 1};
  if (center.x == r.left || center.y == r.top)
    return 0;
  Point first{Word(U(start.x - center.x)), Word(U(center.y - start.y))},
      last{Word(U(end.x - center.x)), Word(U(center.y - end.y))};
  int di = Locate(out, int(n), center, first),
      si = Locate(out, int(n), center, last);
  if (di < 0 || si < 0)
    return 0;
  int previous = di ? di - 1 : int(n) - 1;
  bool small = false;
  if (si == di || si == previous) {
    if (n + 2 > capacity)
      return 0;
    for (int i = int(n) - 1; i >= di; --i)
      out[i + 2] = out[i];
    n += 2;
    if (si > di)
      si += 2;
    if (previous > di)
      previous += 2;
    out[di + 1] = out[previous];
    const I64 cross = Signed32(U(last.y * first.x) - U(first.y * last.x));
    if ((flags & 0x100) || (si == di && cross > 0)) {
      si += 2;
      small = true;
    } else {
      di += 2;
      previous = di - 1;
    }
  }
  Point delta{SignDifference(out[di].x, out[previous].x),
              SignDifference(out[di].y, out[previous].y)};
  // These loops move at most along the original adjacent vertex segment.
  if (!Above(out[previous], center, first))
    do {
      out[previous] = Step(out[previous], delta, 1);
    } while (!Above(out[previous], center, first));
  Point saved = out[previous];
  out[previous] = Step(out[previous], delta, -1);
  if (PreferSaved(out[previous], saved, center, first))
    out[previous] = saved;
  int begin = previous;
  di = si;
  previous = di ? di - 1 : int(n) - 1;
  delta = {SignDifference(out[di].x, out[previous].x),
           SignDifference(out[di].y, out[previous].y)};
  if (Above(out[di], center, last))
    do {
      out[di] = Step(out[di], delta, -1);
    } while (Above(out[di], center, last));
  saved = out[di];
  out[di] = Step(out[di], delta, 1);
  if (PreferSaved(out[di], saved, center, last))
    out[di] = saved;
  if (small) {
    out[0] = out[begin];
    out[1] = out[di];
    begin = 0;
    di = 1;
  }
  if (di < begin) {
    // GDI rotates through three in-place reversals.
    for (int l = 0, h = begin - 1; l < h; ++l, --h) {
      auto v = out[l];
      out[l] = out[h];
      out[h] = v;
    }
    for (int l = begin, h = int(n) - 1; l < h; ++l, --h) {
      auto v = out[l];
      out[l] = out[h];
      out[h] = v;
    }
    for (int l = 0, h = int(n) - 1; l < h; ++l, --h) {
      auto v = out[l];
      out[l] = out[h];
      out[h] = v;
    }
    di += int(n) - begin;
  } else if (begin) {
    for (int i = begin; i <= di; ++i)
      out[i - begin] = out[i];
    di -= begin;
  }
  if (flags & 4) {
    if (U(++di) >= capacity)
      return 0;
    out[di] = center;
  }
  return U(di + 1);
}
// GDI flags10 plus9CC..0BDC. RoundRect adds the straight
// middle extents to the odd-center offsets before mirroring each quadrant.
inline U RoundVertices(Point *out, U capacity, Rect corner, int gap_x,
                       int gap_y) {
  int cx = Word(U(corner.left + corner.right)) >> 1,
      cy = Word(U(corner.top + corner.bottom)) >> 1;
  int ox = Word(U((U(corner.left + corner.right) & 1) + gap_x)),
      oy = Word(U((U(corner.top + corner.bottom) & 1) + gap_y));
  int rx = cx - corner.left, ry = cy - corner.top;
  if (!rx || !ry) {
    if (!(rx + ox) || !(ry + oy) || capacity < 4)
      return 0;
    out[0] = {cx + rx + ox, cy - ry};
    out[1] = {cx - rx, cy - ry};
    out[2] = {cx - rx, cy + ry + oy};
    out[3] = {cx + rx + ox, cy + ry + oy};
    return 4;
  }
  U n = Quadrant(out, capacity, rx >= ry ? rx : ry, rx >= ry ? ry : rx);
  if (ry > rx) {
    for (U i = 0; i < n; ++i) {
      int v = out[i].x;
      out[i].x = out[i].y;
      out[i].y = v;
    }
    for (U i = 0; i < n / 2; ++i) {
      auto v = out[i];
      out[i] = out[n - 1 - i];
      out[n - 1 - i] = v;
    }
  }
  U mirrored = n - (ox ? 0 : 1), top = n + mirrored, half = top - (oy ? 0 : 1),
    total = half * 2;
  if (total > capacity)
    return 0;
  for (U i = 0; i < mirrored; ++i) {
    Point p = out[i];
    out[top - 1 - i] = {cx - p.x, cy - p.y};
    out[i] = {cx + ox + p.x, cy - p.y};
  }
  if (n > mirrored) {
    out[mirrored].x += cx + ox;
    out[mirrored].y = cy - out[mirrored].y;
  }
  for (U i = top - half; i < half; ++i)
    out[total - 1 - (i - (top - half))] = {out[i].x, cy * 2 + oy - out[i].y};
  return total;
}
} // namespace Win98GdiArcs
