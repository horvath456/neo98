// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Native implementation of GDI15 ellipse vertices and GDI37 polygon edges.
namespace Win98GdiShapes {
using U = unsigned;
using I64 = long long;
using U64 = unsigned long long;
inline int Word(U v) { return int(v & 32767) - int(v & 32768); }
// GDI validates the narrowed mode, then7B70 exchanges DC+9A.
inline U SetFillMode(U &state, U value) {
  int mode = Word(value);
  if (mode < 1 || mode > 2)
    return 0;
  U before = state;
  state = U(mode);
  return before;
}
struct Point {
  int x, y;
};
struct Rect {
  int left, top, right, bottom;
};
inline bool Same(Point a, Point b) { return a.x == b.x && a.y == b.y; }
// GDI suppresses redundant axial and monotone diagonal vertices.
inline void EmitQuadrant(Point *out, U capacity, U &count, Point p) {
  if (count >= 2) {
    auto a = out[count - 2], b = out[count - 1];
    if (b.x == p.x) {
      if (a.x == p.x) {
        out[count - 1].y = p.y;
        return;
      }
    } else if (b.y == p.y) {
      if (a.y == p.y) {
        out[count - 1].x = p.x;
        return;
      }
    } else {
      int dx = Word(U(a.x - b.x)), ex = Word(U(b.x - p.x));
      if (dx && ((dx ^ ex) >= 0)) {
        int dy = Word(U(a.y - b.y)), ey = Word(U(b.y - p.y));
        if (dy && ((dy ^ ey) >= 0)) {
          out[count - 1] = p;
          return;
        }
      }
    }
  }
  if (count < capacity)
    out[count++] = p;
}
// GDI and 64-bit arithmetic helpers12F2..1378.
// major/minor are nonnegative WORD radii, major >= minor. No floating point.
inline U Quadrant(Point *out, U capacity, int major, int minor) {
  const I64 a = I64(minor) * minor, b = I64(major) * major;
  I64 tx = I64(major) * a * 2, ty = 0;
  I64 error = (a >> 2) + b - I64(major) * a, second = ((a - b) >> 1) + (a - b);
  int x = major, y = 0;
  U count = 0;
  do {
    EmitQuadrant(out, capacity, count, {x, y});
    if (error >= 0) {
      --x;
      tx -= a * 2;
      error -= tx;
    }
    ++y;
    ty += b * 2;
    error += ty + b;
  } while (U64(ty) < U64(tx));
  if (x >= 0) {
    second = (second - ty - tx) >> 1;
    error += second;
    bool emit = !(error >= 0 && count && out[count - 1].x == x);
    do {
      if (emit) {
        EmitQuadrant(out, capacity, count, {x, y});
        if (error < 0) {
          ++y;
          ty += b * 2;
          error += ty;
        }
      }
      --x;
      tx -= a * 2;
      error -= tx;
      error += a;
      emit = true;
    } while (x >= 0);
  }
  return count;
}
// GDI (Ellipse flags8),9CC..BDC and10E3 axis swap/reversal.
inline U EllipseVertices(Point *out, U capacity, Rect r) {
  int cx = Word(U(r.left + r.right)) >> 1, cy = Word(U(r.top + r.bottom)) >> 1;
  const int oddx = U(r.left + r.right) & 1, oddy = U(r.top + r.bottom) & 1;
  int rx = cx - (r.left < r.right ? r.left : r.right),
      ry = cy - (r.top < r.bottom ? r.top : r.bottom);
  if (!rx || !ry) {
    if (!(rx + oddx) || !(ry + oddy) || capacity < 4)
      return 0;
    out[0] = {cx + rx + oddx, cy - ry};
    out[1] = {cx - rx, cy - ry};
    out[2] = {cx - rx, cy + ry + oddy};
    out[3] = {cx + rx + oddx, cy + ry + oddy};
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
  U mirrored = n - (oddx ? 0 : 1), top = n + mirrored,
    half = top - (oddy ? 0 : 1), total = half * 2;
  if (total > capacity)
    return 0;
  for (U i = 0; i < mirrored; ++i) {
    Point p = out[i];
    out[top - 1 - i] = {cx - p.x, cy - p.y};
    out[i] = {cx + oddx + p.x, cy - p.y};
  }
  if (n > mirrored) {
    out[mirrored].x += cx + oddx;
    out[mirrored].y = cy - out[mirrored].y;
  }
  for (U i = top - half; i < half; ++i)
    out[total - 1 - (i - (top - half))] = {out[i].x, cy * 2 + oddy - out[i].y};
  return total;
}
// GDI37 edge record is32 bytes. WORD arithmetic in the original is retained.
struct Edge {
  int x = 0, twice_minor = 0, y_major = 0, error = 0, diagonal = 0, step = 0;
  int start = 0, length = 0, index = 0, y0 = 0, y1 = 0, end = 0;
  U direction = 0, visible = 1;
};
// GDI.
inline void EdgeError(Edge &e, int dx, int dy) {
  int x = Word(U(dx)), y = Word(U(dy));
  int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, bias = 1, minor = ay,
      major = ax;
  e.y_major = 0;
  if (ax <= ay) {
    e.y_major = 1;
    minor = ax;
    major = ay;
    if (x < 0)
      --bias;
  }
  int doubled = Word(U(minor * 2));
  if (doubled < 0)
    major >>= 1;
  else
    minor = doubled;
  e.twice_minor = Word(U(minor));
  e.error = Word(U(bias + minor - major));
  e.diagonal = Word(U(minor - major * 2));
}
// GDI initializes the first segment of a monotone chain.
inline void StartEdge(Edge &e, const Point *p) {
  e.index = e.start + (e.length < 0 ? -1 : 0);
  e.y0 = p[e.start].y;
  e.y1 = p[e.start + e.length].y;
  Point a = p[e.index], b = p[e.index + 1];
  int dx = Word(U(b.x - a.x)), dy = Word(U(b.y - a.y));
  e.step = dx > 0 ? 1 : dx < 0 ? -1 : 0;
  if (dy < 0) {
    e.step = -e.step;
    dx = Word(U(-dx));
  }
  e.x = e.length < 0 ? b.x : a.x;
  if (e.length < 0)
    dy = Word(U(-dy));
  e.end = Word(U(e.y0 + dy));
  e.visible = 1;
  EdgeError(e, dx, dy);
}
// GDI, preserving horizontal vertices inside each monotone chain.
inline U BuildEdges(Edge *out, const Point *p, U start, U count) {
  if (count <= 1)
    return 0;
  U n = 0;
  bool up = false, down = false;
  Edge e;
  e.start = int(start);
  const U end = start + count - 1;
  for (U i = start; i < end; ++i) {
    int a = p[i].y, b = p[i + 1].y;
    if (a < b) {
      if (up) {
        e.length = e.start - int(i);
        e.start = int(i);
        e.direction = 2;
        StartEdge(e, p);
        out[n++] = e;
        e = {};
        e.start = int(i);
        up = false;
      }
      down = true;
    } else if (a > b) {
      if (down) {
        e.length = int(i) - e.start;
        e.direction = 1;
        StartEdge(e, p);
        out[n++] = e;
        e = {};
        e.start = int(i);
        down = false;
      }
      up = true;
    }
  }
  if (up) {
    e.length = e.start - int(end);
    e.start = int(end);
    e.direction = 2;
  } else {
    e.length = int(end) - e.start;
    e.direction = 1;
  }
  StartEdge(e, p);
  out[n++] = e;
  return n;
}
// GDI stable insertion sort. Original uses signed WORD keys.
inline void SortEdges(Edge *e, U count, bool by_y) {
  for (U i = 1; i < count; ++i) {
    Edge value = e[i];
    U j = i;
    while (j && (by_y ? e[j - 1].y0 > value.y0 : e[j - 1].x > value.x)) {
      e[j] = e[j - 1];
      --j;
    }
    e[j] = value;
  }
}
// GDI selects winding boundaries and suppresses coincident edges.
inline void Winding(Edge *e, U count) {
  for (U i = 0; i < count; ++i)
    e[i].visible = 1;
  int winding = 0;
  for (U i = 0; i + 1 < count; ++i) {
    if (!e[i].visible)
      continue;
    for (U j = i + 1; j < count; ++j)
      if (e[i].index == e[j].index) {
        if (!winding)
          e[j].visible = 0;
        else
          e[i].visible = 0;
        break;
      }
    int before = winding;
    if (e[i].direction & 1)
      ++winding;
    else if (e[i].direction & 2)
      --winding;
    if (before && winding)
      e[i].visible = 0;
  }
}
// GDI scanline error recurrence.
inline void StepEdge(Edge &e) {
  if (e.step == 1000)
    return;
  if (e.y_major) {
    if (e.error > 0) {
      e.x = Word(U(e.x + e.step));
      e.error = Word(U(e.error + e.diagonal));
    } else
      e.error = Word(U(e.error + e.twice_minor));
  } else {
    int error = Word(U(e.error + e.diagonal)), x = Word(U(e.x + e.step));
    while (error <= 0) {
      x = Word(U(x + e.step));
      error = Word(U(error + e.twice_minor));
    }
    e.error = error;
    e.x = x;
  }
}
// GDI advances through original polyline segments, including
// horizontal runs.
inline void AdvanceEdge(Edge &e, const Point *p, int y) {
  while (y >= e.end) {
    e.index += e.length >= 0 ? 1 : -1;
    auto a = p[e.index], b = p[e.index + 1];
    int dx = Word(U(b.x - a.x)), dy = Word(U(b.y - a.y));
    e.end = e.length >= 0 ? b.y : a.y;
    e.x = e.length >= 0 ? a.x : b.x;
    if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
      e.step = 1000;
      continue;
    }
    e.step = dx > 0 ? 1 : dx < 0 ? -1 : 0;
    if (dy < 0) {
      e.step = -e.step;
      dx = Word(U(-dx));
    }
    EdgeError(e, dx, dy);
  }
}
// Native scratch allocation and clipped span storage are providers. Ordering,
// winding selection, zero-span suppression, joining and stepping followGDI37.
template <class E>
bool PolygonSpans(E &provider, Edge *edges, U count, const Point *p, int bottom,
                  U fill_mode) {
  if (!count)
    return true;
  SortEdges(edges, count, true);
  U begin = 0, end = 0;
  int y = edges[0].y0;
  while (y < bottom) {
    while (end < count && edges[end].y0 == y)
      ++end;
    for (U i = begin; i < end; ++i) {
      if (y >= edges[i].y1) {
        auto replacement = edges[begin++];
        edges[i] = replacement;
        continue;
      }
      if (y >= edges[i].end)
        AdvanceEdge(edges[i], p, y);
    }
    if (begin == end) {
      if (end == count)
        break;
      y = edges[end].y0;
      continue;
    }
    SortEdges(edges + begin, end - begin, false);
    if (!(fill_mode & 1))
      Winding(edges + begin, end - begin);
    bool pending = false;
    int left = 0, run_left = 0, run_right = 0;
    bool run = false;
    for (U i = begin; i < end; ++i) {
      auto &e = edges[i];
      if (e.visible) {
        if (!pending) {
          left = e.x;
          pending = true;
        } else {
          pending = false;
          if (left != e.x) {
            if (run && run_right == left)
              run_right = e.x;
            else {
              if (run && !provider.Span(y, run_left, run_right))
                return false;
              run = true;
              run_left = left;
              run_right = e.x;
            }
          }
        }
      }
      StepEdge(e);
    }
    if (run && !provider.Span(y, run_left, run_right))
      return false;
    ++y;
  }
  return true;
}
// GDI,04BE..0513,0700..0832. Wide solid rectangles
// use disjoint horizontal spans, including when the pen covers the interior.
template <class E> U WideRectangle(E &e, Rect r, int width, bool fill) {
  if (r.left == r.right || r.top == r.bottom) return 1;
  if (r.left > r.right) { int v=r.left; r.left=r.right; r.right=v; }
  if (r.top > r.bottom) { int v=r.top; r.top=r.bottom; r.bottom=v; }
  r.left=Word(U(r.left-width/2)); r.top=Word(U(r.top-width/2));
  r.right=Word(U(r.right+(width-1)/2));
  r.bottom=Word(U(r.bottom+(width-1)/2));
  Rect inner{Word(U(r.left+width)),Word(U(r.top+width)),
             Word(U(r.right-width)),Word(U(r.bottom-width))};
  if (fill && inner.left<inner.right && inner.top<inner.bottom) e.Fill(inner);
  int left=inner.left<r.right?inner.left:r.right;
  int right=inner.right>left?inner.right:left;
  for (int y=inner.top;y<inner.bottom;++y) {
    e.PenSpan(y,r.left,left); e.PenSpan(y,right,r.right);
  }
  int top=inner.top<r.bottom-1?inner.top:r.bottom-1;
  for (int y=r.top;y<top;++y) e.PenSpan(y,r.left,r.right);
  int bottom=inner.bottom>top?inner.bottom:top;
  for (int y=bottom;y<r.bottom;++y) e.PenSpan(y,r.left,r.right);
  return 1;
}
// GDI, solid-pen Ellipse branch. The input bounds already
// exclude right/bottom (GDI). The inner contour is filled with the
// brush, then the closed inner and outer contours form an alternate-fill
// polygon in the pen colour. No rounded-polyline approximation is involved.
inline U WideEllipseContours(Point *points,U capacity,Rect r,int width,U &inner) {
  Rect in{Word(U(r.left+(width+1)/2)),Word(U(r.top+(width+1)/2)),
          Word(U(r.right-width/2)),Word(U(r.bottom-width/2))};
  inner=0;
  if (in.left<=in.right && in.top<=in.bottom) {
    inner=EllipseVertices(points,capacity,in);
    if (inner>2) {
      if (inner>=capacity) return 0;
      points[inner]=points[0]; ++inner;
    } else inner=0;
  }
  Rect outer{Word(U(in.left-width)),Word(U(in.top-width)),
             Word(U(in.right+width)),Word(U(in.bottom+width))};
  U n=EllipseVertices(points+inner,capacity-inner,outer);
  if (n<=2 || inner+n>=capacity) return 0;
  points[inner+n]=points[inner];
  return inner+n+1;
}
// GDI,0205..023D,02B2..02BA,0319..0341,
// 04BE..0513 and0617..0660. Thin/null-pen software rectangle decisions.
template <class E> U Rectangle(E &e, Rect r, U pen_style, bool fill) {
  if (r.left == r.right || r.top == r.bottom)
    return 1;
  if (r.left > r.right) {
    int v = r.left;
    r.left = r.right;
    r.right = v;
  }
  if (r.top > r.bottom) {
    int v = r.top;
    r.top = r.bottom;
    r.bottom = v;
  }
  const int width = pen_style == 5 ? 0 : 1;
  if (pen_style != 6) {
    r.right = Word(U(r.right + ((width - 1) >> 1)));
    r.bottom = Word(U(r.bottom + ((width - 1) >> 1)));
  }
  Rect inner{Word(U(r.left + width)), Word(U(r.top + width)),
             Word(U(r.right - width)), Word(U(r.bottom - width))};
  if (fill && inner.left < inner.right && inner.top < inner.bottom)
    e.Fill(inner);
  if (pen_style != 5) {
    Point points[] = {{r.left, r.top},
                      {Word(U(r.right - 1)), r.top},
                      {Word(U(r.right - 1)), Word(U(r.bottom - 1))},
                      {r.left, Word(U(r.bottom - 1))},
                      {r.left, r.top}};
    e.Outline(points, 5);
  }
  return 1;
}

} // namespace Win98GdiShapes
