// SPDX-License-Identifier: GPL-2.0-or-later
// GDI region construction, region painting, logical brushes, and bitmap
// dimensions.
namespace {
constexpr const char *RegionBrushNames[] = {
    "CreateEllipticRgn",    "CreateEllipticRgnIndirect",
    "CreateRoundRectRgn",   "FillRgn",
    "FrameRgn",             "InvertRgn",
    "PaintRgn",             "CreateHatchBrush",
    "CreateBrushIndirect",  "UnrealizeObject",
    "GetBitmapDimensionEx", "SetBitmapDimensionEx"};
constexpr ApiIdTable RegionBrushIds(RegionBrushNames);
// GDI keeps the rectangle it was handed, and the shared conversion
// turns it into the inclusive box the vertex kernels expect.
struct ShapeBox { int left, top, right, bottom; };
bool InclusiveBox(ShapeBox &r) {
  --r.right;
  --r.bottom;
  return r.right > r.left && r.bottom > r.top &&
         r.left + r.right >= -32768 && r.left + r.right <= 32767 &&
         r.top + r.bottom >= -32768 && r.top + r.bottom <= 32767;
}
// Turns the closed vertex ring of a shape kernel into a region, one scan line
// at a time. The rings are convex and already name the outline pixels.
unsigned RegionFromVertices(const Win98GdiShapes::Point *points, unsigned count) {
  RegionSerial serial;
  auto &e = region_store;
  auto &s = e.state;
  const auto *ring = reinterpret_cast<const Win98GdiRegionShape::Point *>(points);
  int top = ring[0].y, bottom = ring[0].y;
  for (unsigned i = 1; i < count; ++i) {
    if (ring[i].y < top) top = ring[i].y;
    if (ring[i].y > bottom) bottom = ring[i].y;
  }
  const unsigned rows = unsigned(bottom - top) + 1;
  auto *extents = new int[rows * 2];
  Win98GdiRegionShape::ConvexRows(ring, count, top, bottom, extents, extents + rows);
  unsigned region = Win98RegionApi::Create(e, s, 0, 0, 0, 0);
  for (unsigned i = 0; region && i < rows; ++i) {
    if (extents[i] > extents[rows + i])
      continue;
    const unsigned span =
        Win98RegionApi::Create(e, s, unsigned(extents[i]), unsigned(top + int(i)),
                               unsigned(extents[rows + i] + 1), unsigned(top + int(i) + 1));
    if (!span) {
      e.Destroy(region);
      region = 0;
      break;
    }
    Win98RegionApi::Combine(e, s, region, region, span, 2);
    e.Destroy(span);
  }
  delete[] extents;
  return region;
}
unsigned RectangleRegion(int left, int top, int right, int bottom) {
  RegionSerial serial;
  return Win98RegionApi::Create(region_store, region_store.state, unsigned(left),
                                unsigned(top), unsigned(right), unsigned(bottom));
}
// GDI (ellipse, flags 88h) and (round rectangle, flags 90h).
// Both reach the vertex kernels with a null device context, so no mapping
// happens and the rectangle stays the caller's. A zero corner extent falls back
// to the rectangle region, exactly as Windows 98 does.
unsigned ShapeRegion(ShapeBox box, bool rounded, int corner_x, int corner_y,
                     BitmapIO &io) {
  RegionSerial outer;
  if (!region_store.Initialize()) {
    io.Error(8);
    return 0;
  }
  const ShapeBox plain = box;
  if ((rounded && (!corner_x || !corner_y)) || !InclusiveBox(box))
    return RectangleRegion(plain.left, plain.top, plain.right, plain.bottom);
  int width = box.right - box.left, height = box.bottom - box.top;
  int gap_x = 0, gap_y = 0;
  if (rounded) {
    if (corner_x < 0) corner_x = -corner_x;
    if (corner_y < 0) corner_y = -corner_y;
    if (corner_x > width) corner_x = width;
    if (corner_y > height) corner_y = height;
    gap_x = width - corner_x;
    gap_y = height - corner_y;
    box.right = box.left + corner_x;
    box.bottom = box.top + corner_y;
    width = corner_x;
    height = corner_y;
  }
  const unsigned capacity = unsigned(width + height + 32);
  if (capacity > 16383) {
    io.Error(8);
    return 0;
  }
  auto *points = new Win98GdiShapes::Point[capacity + 1];
  const Win98GdiShapes::Rect shape{box.left, box.top, box.right, box.bottom};
  const unsigned count =
      rounded ? Win98GdiArcs::RoundVertices(points, capacity, shape, gap_x, gap_y)
              : Win98GdiShapes::EllipseVertices(points, capacity, shape);
  unsigned region = count >= 2 ? RegionFromVertices(points, count) : 0;
  delete[] points;
  if (!region)
    io.Error(8);
  return region;
}
// GDI, and paint every rectangle of the region with the
// DC's brush. The rectangles are logical, so each one takes the DC's mapping.
bool PaintRegionRectangles(Context &dc, unsigned region, unsigned rop,
                           const DibEng::Brush &brush) {
  const unsigned count = RegionRectangles(region, nullptr, 0, 0, 0);
  if (!count)
    return true;
  auto *rectangles = new RECT[count];
  const bool complete = RegionRectangles(region, rectangles, count, 0, 0) == count;
  bool ok = complete;
  for (unsigned i = 0; ok && i < count; ++i) {
    const RECT &r = rectangles[i];
    RECT device;
    ok = Rectangle(dc.current, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   device) &&
         Paint(dc, device, rop, brush);
  }
  delete[] rectangles;
  return ok;
}
// GDI builds the frame as the region minus the part that survives
// a shift by the border extent at all four corners, then paints it. The
// border extent is the DC extent of the two arguments, taken absolute

unsigned FrameRegion(unsigned source, int dx, int dy) {
  RegionSerial serial;
  auto &e = region_store;
  auto &s = e.state;
  const unsigned raw = RawRegion(source);
  if (!raw)
    return 0;
  unsigned inner = e.Clone(raw), shifted = 0, frame = e.Clone(raw);
  if (!inner || !frame) {
    if (inner) e.Destroy(inner);
    if (frame) e.Destroy(frame);
    return 0;
  }
  const int offsets[4][2] = {{dx, dy}, {-dx, dy}, {-dx, -dy}, {dx, -dy}};
  bool ok = true;
  for (const auto &offset : offsets) {
    shifted = e.Clone(raw);
    if (!shifted) { ok = false; break; }
    Win98Region::Offset(e, shifted, unsigned(offset[0]), unsigned(offset[1]));
    ok = Win98RegionApi::Combine(e, s, inner, inner, shifted, 1) != 0;
    e.Destroy(shifted);
    if (!ok) break;
  }
  if (ok)
    ok = Win98RegionApi::Combine(e, s, frame, raw, inner, 4) != 0;
  e.Destroy(inner);
  if (!ok) {
    e.Destroy(frame);
    return 0;
  }
  return frame;
}
} // namespace

// GDI32 dispatches CreateBrushIndirect on lbStyle: 3 and 7 go to
// CreatePatternBrush, 5 and 8 to CreateDIBPatternBrush, 6 to
// CreateDIBPatternBrushPt, and everything else to the word thunk,
// which reaches GDI ->. LOGBRUSH32 is style, color, hatch.
HBRUSH CreateHatchBrush(unsigned style, COLORREF color) {
  if (style >= Win98GdiHatch::Styles)
    return 0;
  const HBRUSH handle = Allocate(BrushObject);
  if (!handle)
    return 0;
  auto *o = Find(handle, BrushObject);
  o->hatch_style = style;
  o->logical_brush_color = color;
  o->brush.color = Pixel(color);
  return handle;
}
bool HatchBrushInfo(HBRUSH handle, unsigned (&info)[3]) {
  const auto *brush = Find(handle, BrushObject);
  if (!brush || brush->hatch_style >= Win98GdiHatch::Styles)
    return false;
  info[0] = 2;
  info[1] = brush->logical_brush_color;
  info[2] = brush->hatch_style;
  return true;
}
bool IsRegionBrushApi(ApiName name) { return RegionBrushIds.Has(name.id); }
unsigned RegionBrushApi(ApiName name, const unsigned *a, BitmapIO &io) {
  struct Boundary {
    BitmapIO *old;
    bool failed;
    Boundary(BitmapIO &io) : old(bitmap_store.io), failed(bitmap_store.failed) {
      bitmap_store.io = &io;
      bitmap_store.failed = false;
    }
    ~Boundary() {
      bitmap_store.io = old;
      bitmap_store.failed = failed;
    }
  } boundary(io);
  const unsigned op = RegionBrushIds.Index(name.id);
  // GDI32 sorts both coordinate pairs before the thunk
  // and Windows 98 reads the four LONGs of the indirect form first.
  if (op <= 2) {
    unsigned r[6] = {a[0], a[1], a[2], a[3], op == 2 ? a[4] : 0u, op == 2 ? a[5] : 0u};
    if (op == 1 && (!a[0] || !io.Read(a[0], r, 16)))
      return 0;
    ShapeBox box{int(r[0]), int(r[1]), int(r[2]), int(r[3])};
    if (box.left > box.right) { const int v = box.left; box.left = box.right; box.right = v; }
    if (box.top > box.bottom) { const int v = box.top; box.top = box.bottom; box.bottom = v; }
    return PublishRegion(ShapeRegion(box, op == 2, int(r[4]), int(r[5]), io));
  }
  // GDI and Windows 98 validate the region, then the brush, then the
  // device context, each against its own signature.
  if (op >= 3 && op <= 6) {
    const bool has_brush = op == 3 || op == 4;
    const unsigned brush_handle = has_brush ? a[2] : 0;
    if (!RawRegion(a[1]))
      return 0;
    const auto *brush = has_brush ? Find(brush_handle, BrushObject) : nullptr;
    if (has_brush && !brush)
      return 0;
    auto *dc = DC(a[0]);
    if (!dc)
      return 0;
    if (!Win98DcInformation::DrawingAllowed(dc->signature))
      return 1;
    unsigned region = a[1], frame = 0;
    if (op == 4) {
      // measures the border as a DC extent and takes it absolute.
      const auto extent = DevicePoint(dc->current, int(a[3]), int(a[4]));
      const auto origin = DevicePoint(dc->current, 0, 0);
      int dx = extent.x - origin.x, dy = extent.y - origin.y;
      if (dx < 0) dx = -dx;
      if (dy < 0) dy = -dy;
      frame = PublishRegion(FrameRegion(a[1], dx, dy));
      if (!frame) {
        io.Error(8);
        return 0;
      }
      region = frame;
    }
    bool result = true;
    if (op == 5) {
      const DibEng::Brush inverting{};
      result = PaintRegionRectangles(*dc, region, 0x00550009u, inverting);
    } else {
      const auto *selected = brush ? brush : Find(dc->current.brush, BrushObject);
      if (!selected)
        result = false;
      else if (!selected->hollow_brush) {
        unsigned pattern[64];
        DibEng::Brush realized;
        result = RealizeBrush(*selected, dc->current, pattern, realized) &&
                 PaintRegionRectangles(*dc, region, 0x00f00021u, realized);
      }
    }
    if (frame)
      DeleteObject(frame);
    return result ? 1 : 0;
  }
  // GDI rejects a hatch index above five before it builds the LOGBRUSH
  // and enters the shared creator.
  if (op == 7) {
    if (a[0] >= Win98GdiHatch::Styles) {
      io.Error(87);
      return 0;
    }
    const HBRUSH handle = CreateHatchBrush(a[0], a[1]);
    if (!handle)
      io.Error(8);
    return handle;
  }
  if (op == 8) {
    unsigned logical[3];
    if (!a[0] || !io.Read(a[0], logical, sizeof logical))
      return 0;
    switch (logical[0]) {
    case 0: { // BS_SOLID
      const HBRUSH handle = CreateSolidBrush(logical[1]);
      if (!handle)
        io.Error(8);
      return handle;
    }
    case 1: { // BS_NULL
      const HBRUSH handle = CreateNullBrush();
      if (!handle)
        io.Error(8);
      return handle;
    }
    case 2: { // BS_HATCHED
      if (logical[2] >= Win98GdiHatch::Styles) {
        io.Error(87);
        return 0;
      }
      const HBRUSH handle = CreateHatchBrush(logical[2], logical[1]);
      if (!handle)
        io.Error(8);
      return handle;
    }
    case 3: case 7: { // BS_PATTERN, BS_PATTERN8X8 ->
      const HBRUSH handle = CreatePatternBrush(logical[2]);
      if (!handle)
        io.Error(87);
      return handle;
    }
    case 5: case 6: case 8: // BS_DIBPATTERN(8X8), BS_DIBPATTERNPT
      io.Fault("CreateBrushIndirect with a DIB pattern needs CreateDIBPatternBrush, which is not ported");
      return 0;
    default:
      // The original reaches the word thunk with the style it was given and
      // only fails when the brush is realized. NEO98 refuses the unusable
      // object here instead.
      io.Error(87);
      return 0;
    }
  }
  if (op == 9) {
    if (!a[0]) return 0; // Win98 leaves LastError unchanged for NULL.
    // GDI accepts the brush, font and palette signatures; marks
    // the realization chain of a brush or a palette stale and answers TRUE.
    // Native brushes and palettes realize at every use, so nothing is cached
    // and the answer is the original's.
    if (Find(a[0], BrushObject) || Find(a[0], PaletteObject))
      return 1;
    io.Error(6);
    return 0;
  }
  // GDI and Windows 98 keep a WORD pair in the bitmap object. The GDI32
  // wrappers narrow both arguments to WORDs and widen the previous
  // pair back to two LONGs. Set answers the previous pair, Get the current one.
  auto *bitmap = Find(a[0], BitmapObject);
  if (!bitmap) {
    io.Error(6);
    return 0;
  }
  const int previous[2] = {bitmap->dimension_x, bitmap->dimension_y};
  if (op == 11) {
    bitmap->dimension_x = Win98GdiShapes::Word(a[1]);
    bitmap->dimension_y = Win98GdiShapes::Word(a[2]);
  }
  const unsigned sink = op == 11 ? a[3] : a[1];
  const int out[2] = {op == 11 ? previous[0] : bitmap->dimension_x,
                      op == 11 ? previous[1] : bitmap->dimension_y};
  if (op == 10 && !sink)
    return 0;
  if (sink && !io.Write(sink, out, sizeof out))
    return 0;
  return 1;
}
