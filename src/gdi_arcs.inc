// SPDX-License-Identifier: GPL-2.0-or-later
// Native ABI/storage providers for the Windows 98-compatible arc/round
// kernels.
bool IsArcApi(ApiName n){switch(n.id){API_CASE("Arc") API_CASE("Pie") API_CASE("Chord") API_CASE("RoundRect") API_CASE("ArcTo") API_CASE("AngleArc") return true;default:return false;}}
unsigned ArcApi(ApiName name, const unsigned *a, BitmapIO &io) {
  using namespace Win98GdiArcs;
  // These two exports really are unsupported in this Win98 GDI32 image.
  // /27988 -> KERNEL32 ordinal17: LastError120, EAX0, stdcall cleanup.
  if (API_IS(name,"ArcTo") || API_IS(name,"AngleArc")) {
    io.Error(120);
    return 0;
  }
  auto *dc = DC(a[0]);
  if (!dc) {
    io.Error(87);
    return 0;
  }
  if (!Win98DcInformation::DrawingAllowed(dc->signature))
    return 0;
  bool rounded = API_IS(name,"RoundRect");
  unsigned flags = rounded                ? 16
                   : API_IS(name,"Arc") ? 1
                   : API_IS(name,"Pie") ? 4
                                          : 2;
  if (rounded && (!Word(a[5]) || !Word(a[6])))
    return ShapeApi("Rectangle", a, io);
  Point logical_a{Word(a[1]), Word(a[2])}, logical_b{Word(a[3]), Word(a[4])};
  auto first = ShapePoint(dc->current, a[1], a[2]),
       last = ShapePoint(dc->current, a[3], a[4]);
  Rect r{first.x, first.y, last.x, last.y};
  Point start{}, end{};
  int corner_x = 0, corner_y = 0;
  if (rounded) {
    // GDI maps the corner dimensions as a point relative to the
    // original first corner, then04AD..04E7 measures/clamps their device
    // extent.
    int x = Word(a[5]), y = Word(a[6]);
    if (x < 0)
      x = -x;
    if (y < 0)
      y = -y;
    if ((a[1] & 65535) >= (a[3] & 65535))
      x = -x;
    if ((a[2] & 65535) >= (a[4] & 65535))
      y = -y;
    auto corner =
        ShapePoint(dc->current, U(logical_a.x + x), U(logical_a.y + y));
    corner_x = Word(U(corner.x - first.x));
    corner_y = Word(U(corner.y - first.y));
    if (corner_x < 0)
      corner_x = -corner_x;
    if (corner_y < 0)
      corner_y = -corner_y;
    if (!corner_x || !corner_y)
      return ShapeApi("Rectangle", a, io);
  } else {
    start = ShapePoint(dc->current, a[5], a[6]);
    end = ShapePoint(dc->current, a[7], a[8]);
    // GDI preserves the high-WORD signed cross-product test for
    // two different logical endpoints that mapping collapses to one device
    // point.
    if ((flags & 5) && Same(start, end)) {
      Point center{Word(U(logical_a.x + logical_b.x)) >> 1,
                   Word(U(logical_a.y + logical_b.y)) >> 1};
      int sx = Word(U(Word(a[5]) - center.x)),
          sy = Word(U(Word(a[6]) - center.y)),
          ex = Word(U(Word(a[7]) - center.x)),
          ey = Word(U(Word(a[8]) - center.y));
      U cross = U(sx * ey) - U(ex * sy);
      if (Word(cross >> 16) > 0) {
        flags |= 0x100;
        if (!(flags & 4))
          return 0;
      }
    }
  }
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
  --r.right;
  --r.bottom;
  int width = r.right - r.left, height = r.bottom - r.top;
  if (width <= 0 || height <= 0)
    return 1;
  if (width > 32767 || height > 32767) {
    io.Error(87);
    return 0;
  }
  int gap_x = 0, gap_y = 0;
  if (rounded) {
    if (corner_x > width)
      corner_x = width;
    if (corner_y > height)
      corner_y = height;
    gap_x = width - corner_x;
    gap_y = height - corner_y;
    r.right = r.left + corner_x;
    r.bottom = r.top + corner_y;
    width = corner_x;
    height = corner_y;
  }
  if (r.left + r.right < -32768 || r.left + r.right > 32767 ||
      r.top + r.bottom < -32768 || r.top + r.bottom > 32767) {
    io.Error(87);
    return 0;
  }
  unsigned capacity = U(width + height + 32);
  if (capacity > 16383) {
    io.Error(8);
    return 0;
  }
  ShapeSink sink{*dc, io};
  if (!sink.Prepare(flags != 1))
    return 0;
  DibTemporary memory;
  if (!memory.Allocate((capacity + 1) * sizeof(Point))) {
    io.Error(8);
    return 0;
  }
  auto *p = reinterpret_cast<Point *>(memory.data);
  unsigned count = rounded ? RoundVertices(p, capacity, r, gap_x, gap_y)
                           : ArcVertices(p, capacity, r, start, end, flags);
  if (count < 2) {
    if (rounded)
      return ShapeApi("Rectangle", a, io);
    return 0;
  }
  if (flags & 1) {
    sink.Outline(p, count);
    return sink.ok;
  }
  if (!Same(p[0], p[count - 1]))
    p[count++] = p[0];
  return sink.Polygons(p, &count, 1, count, 1);
}
