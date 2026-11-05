// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The path store of a device context and the curve flattener, implemented
// from GDI.EXE segments 33 (the path family) and 32 (the Bezier stage).
//
// Windows 98 keeps the path of a device context in two fields of the DC
// itself: the WORD at DC+78h is the handle of the path object, the byte at
// DC+7Ah carries three bits. GDI (ABORTPATH), (BEGINPATH),
// (ENDPATH) and (FILLPATH) read and write exactly these two
// fields, so the whole family fits into the flags below.
namespace Win98GdiPath {
// The three path state bits stored in the DC.
constexpr unsigned Open = 1;             // BeginPath ran, EndPath did not
constexpr unsigned Curved = 2;           // the path still holds Bezier records
constexpr unsigned CounterClockwise = 4; // SetArcDirection(AD_COUNTERCLOCKWISE)
// The point types Win32 hands back through GetPath.
constexpr unsigned char CloseFigureType = 1, LineToType = 2, BezierToType = 4,
                        MoveToType = 6;
struct Point {
  int x, y;
};

// Return two when bit 2 is set, otherwise one.
inline unsigned ArcDirection(unsigned flags) {
  return flags & CounterClockwise ? 2 : 1;
}
// GDI rejects anything outside 1..2 with error 6001h before it reaches
// the body; the body answers the previous direction.
inline bool SetArcDirection(unsigned &flags, unsigned direction,
                            unsigned &previous) {
  if (direction != 1 && direction != 2)
    return false;
  previous = ArcDirection(flags);
  if (direction == 2)
    flags |= CounterClockwise;
  else
    flags &= ~CounterClockwise;
  return true;
}

// --- the Bezier stage, GDI segment 32 -------------------------------------
//
// GDI takes one cubic as eight 28.4 fixed point values and turns it
// into a run of line segments. It has two stages. tries to set up a
// forward difference run; if that fails, splits the curve at its
// midpoint and calls itself for both halves, with a stack guard of 400h bytes.
inline int Shl(int value, unsigned count) {
  return int(unsigned(value) << (count & 31));
}
inline int Sar(int value, unsigned count) { return value >> (count & 31); }
inline int Abs(int value) { return value < 0 ? -value : value; }
inline int Triple(int value) { return int(unsigned(value) * 3u); }

// The forward difference state. Every field is one of the DWORDs the
// original keeps in its 44h byte frame at ESI.
struct Forward {
  int ax, bx, cx, dx; // the x polynomial
  int ay, by, cy, dy; // the y polynomial
  int origin_x, origin_y;
  int steps;
};
// Answers false when the control polygon does not fit into 14 bits
// after the move to its own minimum; the caller then subdivides.
inline bool Setup(Forward &f, const int *c) {
  int x[4] = {c[0], c[2], c[4], c[6]}, y[4] = {c[1], c[3], c[5], c[7]};
  int minimum_x = x[0], minimum_y = y[0];
  for (unsigned i = 1; i < 4; ++i) {
    if (x[i] < minimum_x) minimum_x = x[i];
    if (y[i] < minimum_y) minimum_y = y[i];
  }
  int merged = 0;
  for (unsigned i = 0; i < 4; ++i) {
    x[i] = int(unsigned(x[i]) - unsigned(minimum_x));
    y[i] = int(unsigned(y[i]) - unsigned(minimum_y));
    merged |= x[i] | y[i];
  }
  // and Windows 98 test the merged value against FFFFC000h.
  if (unsigned(merged) & 0xffffc000u)
    return false;
  // without the fixed point output flag both origins round to whole
  // units. The path family never sets that flag.
  f.origin_x = Sar(int(unsigned(minimum_x) + 8), 4);
  f.origin_y = Sar(int(unsigned(minimum_y) + 8), 4);
  f.ax = Shl(x[0], 10);
  f.bx = Shl(int(unsigned(x[3]) - unsigned(x[0])), 10);
  f.dx = Shl(Triple(int(unsigned(x[0]) - 2u * unsigned(x[1]) + unsigned(x[2]))), 11);
  f.cx = Shl(Triple(int(unsigned(x[1]) - 2u * unsigned(x[2]) + unsigned(x[3]))), 11);
  f.ay = Shl(y[0], 10);
  f.by = Shl(int(unsigned(y[3]) - unsigned(y[0])), 10);
  f.dy = Shl(Triple(int(unsigned(y[0]) - 2u * unsigned(y[1]) + unsigned(y[2]))), 11);
  f.cy = Shl(Triple(int(unsigned(y[1]) - 2u * unsigned(y[2]) + unsigned(y[3]))), 11);
  // doubles the number of steps until the two curvature terms
  // fit into the low sixteen bits, shifted by the step exponent.
  int steps = 1;
  unsigned k = 0;
  while (Shl(int(0xffff0000u), k) &
         (Abs(f.dy) | Abs(f.dx) | Abs(f.cy) | Abs(f.cx))) {
    k += 2;
    f.cy = Sar(int(unsigned(f.cy) + unsigned(f.dy)), 1);
    f.by = Sar(int(unsigned(f.by) - unsigned(Sar(f.cy, k))), 1);
    f.cx = Sar(int(unsigned(f.cx) + unsigned(f.dx)), 1);
    f.bx = Sar(int(unsigned(f.bx) - unsigned(Sar(f.cx, k))), 1);
    steps += steps;
    if (steps > (1 << 16))
      return false; // the original runs out of significance long before this
  }
  // the position and the first difference gain three bits, the two
  // curvature terms move by the step exponent minus three.
  f.ax = Shl(f.ax, 3);
  f.bx = Shl(f.bx, 3);
  f.ay = Shl(f.ay, 3);
  f.by = Shl(f.by, 3);
  const int shift = int(k) - 3;
  if (shift >= 0) {
    f.cx = Sar(f.cx, unsigned(shift));
    f.dx = Sar(f.dx, unsigned(shift));
    f.cy = Sar(f.cy, unsigned(shift));
    f.dy = Sar(f.dy, unsigned(shift));
  } else {
    f.cx = Shl(f.cx, unsigned(-shift));
    f.dx = Shl(f.dx, unsigned(-shift));
    f.cy = Shl(f.cy, unsigned(-shift));
    f.dy = Shl(f.dy, unsigned(-shift));
  }
  // performs the first step inside the setup and leaves the caller
  // with one step less to walk.
  f.steps = steps;
  return true;
}
// one forward difference step for both polynomials.
inline void Step(Forward &f) {
  f.ay = int(unsigned(f.ay) + unsigned(f.by));
  const int next_by = int(unsigned(f.by) + unsigned(f.cy));
  const int next_cy = int(2u * unsigned(f.cy) - unsigned(f.dy));
  f.dy = f.cy;
  f.by = next_by;
  f.cy = next_cy;
  f.ax = int(unsigned(f.ax) + unsigned(f.bx));
  const int next_bx = int(unsigned(f.bx) + unsigned(f.cx));
  const int next_cx = int(2u * unsigned(f.cx) - unsigned(f.dx));
  f.dx = f.cx;
  f.bx = next_bx;
  f.cx = next_cx;
}
// The run adapts while it walks: a curvature term that
// grows past FFF80000h halves the step, and an even number of remaining steps
// whose doubled terms still fit halves the number of steps instead.
inline void Adapt(Forward &f) {
  if (unsigned(Abs(f.cy) | Abs(f.dy) | Abs(f.cx) | Abs(f.dx)) & 0xfff80000u) {
    f.cy = Sar(int(unsigned(f.cy) + unsigned(f.dy)), 3);
    f.by = Sar(int(unsigned(f.by) - unsigned(f.cy)), 1);
    f.dy = Sar(f.dy, 2);
    f.cx = Sar(int(unsigned(f.cx) + unsigned(f.dx)), 3);
    f.bx = Sar(int(unsigned(f.bx) - unsigned(f.cx)), 1);
    f.dx = Sar(f.dx, 2);
    f.steps += f.steps;
    return;
  }
  if (f.steps & 1)
    return;
  const int wide_dy = Shl(f.dy, 2), wide_cy = int(unsigned(Shl(f.cy, 3)) - unsigned(wide_dy));
  const int wide_dx = Shl(f.dx, 2), wide_cx = int(unsigned(Shl(f.cx, 3)) - unsigned(wide_dx));
  if (unsigned(Abs(wide_dy) | Abs(wide_cy) | Abs(wide_dx) | Abs(wide_cx)) & 0xfff80000u)
    return;
  f.by = int(2u * unsigned(f.by) + unsigned(f.cy));
  f.cy = wide_cy;
  f.dy = wide_dy;
  f.bx = int(2u * unsigned(f.bx) + unsigned(f.cx));
  f.cx = wide_cx;
  f.dx = wide_dx;
  f.steps = Sar(f.steps, 1);
}
// Emit the point in whole units. The path family never selects the
// alternative fixed-point output format.
inline Point At(const Forward &f) {
  return {Sar(int(unsigned(f.ax) + 0x10000u), 17) + f.origin_x,
          Sar(int(unsigned(f.ay) + 0x10000u), 17) + f.origin_y};
}
// `emit` receives every point after the start point of the curve.
// `depth` mirrors the stack guard : the original gives up when less
// than 400h bytes of its 16-bit stack remain.
template <class Emit>
bool Bezier(const int *c, Emit emit, unsigned depth = 0) {
  Forward f{};
  if (Setup(f, c)) {
    Step(f);
    --f.steps;
    for (;;) {
      if (!emit(At(f)))
        return false;
      if (f.steps <= 0)
        return true;
      Adapt(f);
      Step(f);
      --f.steps;
    }
  }
  if (depth >= 32)
    return true; // the original stops subdividing and drops the rest
  // De Casteljau at the midpoint. The left half is
  // (p0, (p0+p1)/2, (p0+2p1+p2)/4, (p0+3p1+3p2+p3)/8), the right half starts
  // at that last point and ends at p3.
  int left[8], right[8];
  for (unsigned axis = 0; axis < 2; ++axis) {
    const int p0 = c[axis], p1 = c[2 + axis], p2 = c[4 + axis], p3 = c[6 + axis];
    const int h1 = Sar(int(unsigned(p0) + unsigned(p1)), 1);
    const int h2 = Sar(int(unsigned(p0) + 2u * unsigned(p1) + unsigned(p2)), 2);
    const int h3 = Sar(int(unsigned(p0) + 3u * unsigned(p1) + 3u * unsigned(p2) +
                           unsigned(p3)),
                       3);
    const int g1 = Sar(int(unsigned(p1) + 2u * unsigned(p2) + unsigned(p3)), 2);
    const int g2 = Sar(int(unsigned(p2) + unsigned(p3)), 1);
    left[axis] = p0;
    left[2 + axis] = h1;
    left[4 + axis] = h2;
    left[6 + axis] = h3;
    right[axis] = h3;
    right[2 + axis] = g1;
    right[4 + axis] = g2;
    right[6 + axis] = p3;
  }
  return Bezier(left, emit, depth + 1) && Bezier(right, emit, depth + 1);
}
// The public form: four points in whole logical units, as every path record
// and every PolyBezier argument carries them. moves a recorded point
// into the 28.4 form with SHL 4.
template <class Emit> bool Curve(const Point *p, Emit emit) {
  const int c[8] = {Shl(p[0].x, 4), Shl(p[0].y, 4), Shl(p[1].x, 4),
                    Shl(p[1].y, 4), Shl(p[2].x, 4), Shl(p[2].y, 4),
                    Shl(p[3].x, 4), Shl(p[3].y, 4)};
  return Bezier(c, emit);
}
} // namespace Win98GdiPath
