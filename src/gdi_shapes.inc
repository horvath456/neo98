// SPDX-License-Identifier: GPL-2.0-or-later
// Guest memory, device realization and retained-bitmap providers for original
// shapes.
namespace {
struct ShapeSink {
  Context &dc;
  BitmapIO &io;
  DibEng::Brush brush{};
  unsigned pattern[64]{};
  bool fill = false, ok = true;
  bool Prepare(bool area = true, bool wide_shapes = false) {
    auto *pen = Find(dc.current.pen, PenObject);
    auto *b = Find(dc.current.brush, BrushObject);
    if (!pen || !b) {
      io.Error(87);
      return false;
    }
    const unsigned style = Win98PenLine::RealizedStyle(pen->pen);
    const int width = Win98PenWidth::Device(pen->pen.width_x, dc.current.mapping.viewport_extent.x,
                                          dc.current.mapping.window_extent.x, dc.current.mapping.shifts.x);
    if (style != 5 && (pen->pen.style > 6 ||
                       ((width > 1 || width < -1) &&
                        (!wide_shapes || pen->pen.style == 6)))) {
      io.Error(50);
      return false;
    }
    // Validate both realizations before any bitmap mutation. Wide solid
    // Rectangle/Ellipse use the original dedicated shape paths below.
    unsigned color;
    auto *bitmap = Find(dc.current.bitmap, BitmapObject);
    if (!bitmap ||
        (style != 5 &&
         (!RealizeDeviceColor(*bitmap, pen->pen.color, color,&dc.current) ||
          !RealizeDeviceColor(*bitmap, Pixel(dc.current.text_mode.background),
                              color)))) {
      io.Error(50);
      return false;
    }
    fill = area && !b->hollow_brush;
    if (fill && !RealizeBrush(*b, dc.current, pattern, brush)) {
      io.Error(50);
      return false;
    }
    return true;
  }
  bool Span(int y, int left, int right) {
    auto &bitmap = Find(dc.current.bitmap, BitmapObject)->bitmap;
    if (y < 0 || y >= bitmap.height)
      return true;
    for (unsigned i = 0; i < dc.current.clips; ++i) {
      auto clip = ClipAt(dc.current, i);
      if (y < clip.top || y >= clip.bottom)
        continue;
      int l = left > clip.left ? left : clip.left,
          r = right < clip.right ? right : clip.right;
      l = l > 0 ? l : 0;
      r = r < bitmap.width ? r : bitmap.width;
      for (int x = l; x < r; ++x) {
        unsigned source =
            brush.pattern
                ? brush.pattern[(unsigned(y) & 7) * 8 + (unsigned(x) & 7)]
                : brush.color;
        DibEng::WritePixel(bitmap, x, y,
                           Win98GdiPixel::Rop(dc.current.text_mode.rop2, source,
                                              DibEng::ReadPixel(bitmap, x, y)));
      }
    }
    return true;
  }
  void Fill(Win98GdiShapes::Rect r) {
    BitmapAccess access(Find(dc.current.bitmap, BitmapObject), true);
    auto &bitmap = Find(dc.current.bitmap, BitmapObject)->bitmap;
    int top = r.top > 0 ? r.top : 0,
        bottom = r.bottom < bitmap.height ? r.bottom : bitmap.height;
    for (int y = top; y < bottom; ++y)
      Span(y, r.left, r.right);
  }
  void PenSpan(int y,int left,int right) {
    auto saved=brush;
    unsigned color=0;
    auto *bitmap=Find(dc.current.bitmap,BitmapObject);
    auto *pen=Find(dc.current.pen,PenObject);
    if (!RealizeDeviceColor(*bitmap,pen->pen.color,color,&dc.current)) {
      io.Error(50); ok=false; return;
    }
    brush={}; brush.color=color;
    BitmapAccess access(bitmap,true);
    Span(y,left,right);
    brush=saved;
  }
  void Outline(const Win98GdiShapes::Point *p, unsigned n) {
    DibTemporary memory;
    if (!memory.Allocate(n * sizeof(Win98PenLine::Point))) {
      io.Error(8);
      ok = false;
      return;
    }
    auto *points = reinterpret_cast<Win98PenLine::Point *>(memory.data);
    for (unsigned i = 0; i < n; ++i)
      points[i] = {p[i].x, p[i].y};
    ok = DrawPenPolyline(dc, points, n, io) && ok;
  }
  bool Polygons(Win98GdiShapes::Point *p, const unsigned *counts,
                unsigned contours, unsigned total, unsigned mode) {
    DibTemporary memory;
    if (!memory.Allocate(total * sizeof(Win98GdiShapes::Edge))) {
      io.Error(8);
      return false;
    }
    auto *edges = reinterpret_cast<Win98GdiShapes::Edge *>(memory.data);
    unsigned edge_count = 0, base = 0;
    int bottom = -32768;
    for (unsigned i = 0; i < total; ++i)
      if (p[i].y > bottom)
        bottom = p[i].y;
    for (unsigned i = 0; i < contours; ++i) {
      edge_count +=
          Win98GdiShapes::BuildEdges(edges + edge_count, p, base, counts[i]);
      base += counts[i];
    }
    if (fill) {
      BitmapAccess access(Find(dc.current.bitmap, BitmapObject), true);
      if (!Win98GdiShapes::PolygonSpans(*this, edges, edge_count, p, bottom,
                                        mode))
        return false;
    }
    base = 0;
    for (unsigned i = 0; i < contours; ++i) {
      if (counts[i] > 1)
        Outline(p + base, counts[i]);
      base += counts[i];
    }
    return ok;
  }
};
Win98GdiShapes::Point ShapePoint(const State &s, unsigned x, unsigned y) {
  auto p = PenDevicePoint(s, x, y);
  return {p.x, p.y};
}
} // namespace
bool IsShapeApi(ApiName n){switch(n.id){API_CASE("Rectangle") API_CASE("Ellipse") API_CASE("Polygon") API_CASE("PolyPolygon") API_CASE("GetPolyFillMode") API_CASE("SetPolyFillMode") return true;default:return false;}}
unsigned ShapeApi(ApiName name, const unsigned *a, BitmapIO &io) {
  using namespace Win98GdiShapes;
  auto *dc = DC(a[0]);
  if (!dc) {
    io.Error(87);
    return 0;
  }
  if (API_IS(name,"GetPolyFillMode"))
    return dc->current.poly_fill_mode;
  if (API_IS(name,"SetPolyFillMode"))
    return SetFillMode(dc->current.poly_fill_mode, a[1]);
  if (!Win98DcInformation::DrawingAllowed(dc->signature))
    return 0;
  ShapeSink sink{*dc, io};
  if (API_IS(name,"Rectangle") || API_IS(name,"Ellipse")) {
    // Public GDI32 thunks narrow coordinates before the original mapping stage.
    if (Word(a[1]) == Word(a[3]) || Word(a[2]) == Word(a[4]))
      return 1;
    if (!sink.Prepare(true, true))
      return 0;
    auto p = ShapePoint(dc->current, a[1], a[2]),
         q = ShapePoint(dc->current, a[3], a[4]);
    Rect r{p.x, p.y, q.x, q.y};
    unsigned style =
        Win98PenLine::RealizedStyle(Find(dc->current.pen, PenObject)->pen);
    int pen_width=Win98PenWidth::Device(Find(dc->current.pen,PenObject)->pen.width_x,
        dc->current.mapping.viewport_extent.x,dc->current.mapping.window_extent.x,dc->current.mapping.shifts.x);
    const bool wide=style!=5 && pen_width>1;
    if (API_IS(name,"Rectangle")) {
      if (wide) {
        WideRectangle(sink,r,pen_width,sink.fill);
        return sink.ok;
      }
      Win98GdiShapes::Rectangle(sink, r, style, sink.fill);
      return sink.ok;
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
    // GDI removes the exclusive right/bottom and validates WORD center
    // sums.
    --r.right;
    --r.bottom;
    int width = r.right - r.left, height = r.bottom - r.top;
    if (!wide && (width <= 0 || height <= 0))
      return 1;
    if (width > 32767 || height > 32767 || r.left + r.right < -32768 ||
        r.left + r.right > 32767 || r.top + r.bottom < -32768 ||
        r.top + r.bottom > 32767) {
      io.Error(87);
      return 0;
    }
    unsigned capacity = unsigned(width + height + 32);
    if (wide) capacity=2*capacity+unsigned(4*pen_width)+2;
    if (capacity > 16383) {
      io.Error(8);
      return 0;
    }
    DibTemporary memory;
    if (!memory.Allocate((capacity + 1) * sizeof(Point))) {
      io.Error(8);
      return 0;
    }
    auto *points = reinterpret_cast<Point *>(memory.data);
    if (wide) {
      unsigned inner=0,total=WideEllipseContours(points,capacity,r,pen_width,inner);
      if (!total) { io.Error(87); return 0; }
      // Allocate the edge store before drawing either contour.
      DibTemporary edge_memory;
      if (!edge_memory.Allocate(total*sizeof(Edge))) { io.Error(8); return 0; }
      auto *edges=reinterpret_cast<Edge *>(edge_memory.data);
      auto raster=[&](unsigned count,unsigned contours,const unsigned *counts) {
        unsigned edge_count=0,base=0;
        int bottom=-32768;
        for(unsigned i=0;i<count;++i) if(points[i].y>bottom) bottom=points[i].y;
        for(unsigned i=0;i<contours;++i) {
          edge_count+=BuildEdges(edges+edge_count,points,base,counts[i]);
          base+=counts[i];
        }
        BitmapAccess access(Find(dc->current.bitmap,BitmapObject),true);
        return PolygonSpans(sink,edges,edge_count,points,bottom,1);
      };
      if(inner && sink.fill && !raster(inner,1,&inner)) return 0;
      unsigned color=0;
      RealizeDeviceColor(*Find(dc->current.bitmap,BitmapObject),
                         Find(dc->current.pen,PenObject)->pen.color,color,&dc->current);
      sink.brush={}; sink.brush.color=color;
      unsigned counts[]={inner,total-inner};
      return inner?raster(total,2,counts):raster(total,1,&total);
    }
    unsigned n = EllipseVertices(points, capacity, r);
    if (n < 2)
      return 0;
    points[n] = points[0];
    ++n;
    return sink.Polygons(points, &n, 1, n, 1);
  }
  bool multi = API_IS(name,"PolyPolygon");
  unsigned contours = multi ? a[3] : 1;
  if (!contours || contours > 0x3ffd) {
    io.Error(87);
    return 0;
  }
  DibTemporary counts_memory;
  if (!counts_memory.Allocate(contours * sizeof(unsigned))) {
    io.Error(8);
    return 0;
  }
  auto *counts = reinterpret_cast<unsigned *>(counts_memory.data);
  unsigned total = 0;
  for (unsigned i = 0; i < contours; ++i) {
    unsigned count = a[2] & 65535;
    if (multi && !io.Read(a[2] + i * 4, &count, 4)) {
      io.Error(87);
      return 0;
    }
    if (count > 0x3ffd || total + count + 1 > 0x3ffd) {
      io.Error(87);
      return 0;
    }
    counts[i] = count;
    total += count + 1;
  }
  if (!multi && counts[0] < 2)
    return 1;
  if (!sink.Prepare())
    return 0;
  DibTemporary memory;
  if (!memory.Allocate(total * sizeof(Point))) {
    io.Error(8);
    return 0;
  }
  auto *points = reinterpret_cast<Point *>(memory.data);
  unsigned source = 0, used = 0;
  for (unsigned i = 0; i < contours; ++i) {
    unsigned count = counts[i], start = used;
    Point first{}, last{};
    for (unsigned j = 0; j < count; ++j) {
      unsigned pair[2];
      if (!io.Read(a[1] + source * 8, pair, sizeof pair)) {
        io.Error(87);
        return 0;
      }
      ++source;
      last = {Word(pair[0]), Word(pair[1])};
      if (!j)
        first = last;
      points[used++] = ShapePoint(dc->current, pair[0], pair[1]);
    }
    // GDI32: closes each contour after WORD narrowing, before mapping.
    if (count && !Same(first, last)) {
      points[used++] = points[start];
      ++count;
    }
    counts[i] = count;
  }
  return sink.Polygons(points, counts, contours, used,
                       dc->current.poly_fill_mode);
}
