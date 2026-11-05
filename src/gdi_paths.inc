// SPDX-License-Identifier: GPL-2.0-or-later
// GDI paths, curves, and flood fills.
namespace {
// Frees the path object a device context still holds. GDI does this
// for DeleteDC and for SETDCSTATE, the worker behind RestoreDC.
void ReleasePath(Context &dc) {
  if (!dc.path)
    return;
  free(dc.path->points);
  free(dc.path->types);
  free(dc.path);
  dc.path = nullptr;
}
constexpr const char *PathNames[] = {
    "BeginPath",      "EndPath",     "AbortPath",   "CloseFigure",
    "FillPath",       "StrokePath",  "StrokeAndFillPath", "FlattenPath",
    "WidenPath",      "PathToRegion","SelectClipPath",    "GetPath",
    "SetArcDirection","GetArcDirection",
    "PolyBezier",     "PolyBezierTo","PolylineTo",  "PolyPolyline",
    "FloodFill",      "ExtFloodFill"};
constexpr ApiIdTable PathIds(PathNames);

// GDI takes the path object from the local heap and grows it while
// records arrive. The native store keeps the same two pieces of information
// per point that GetPath hands back: the logical point and its type byte.
bool ReservePath(Context &dc, unsigned more) {
  if (!dc.path) {
    dc.path = (GdiPath *)calloc(1, sizeof(GdiPath));
    if (!dc.path)
      return false;
  }
  auto &p = *dc.path;
  if (p.count + more <= p.capacity)
    return true;
  unsigned capacity = p.capacity ? p.capacity : 64;
  while (capacity < p.count + more) {
    if (capacity > 0x4000000u)
      return false;
    capacity *= 2;
  }
  auto *points = (Win98GdiPath::Point *)realloc(p.points, capacity * sizeof(Win98GdiPath::Point));
  if (!points)
    return false;
  p.points = points;
  auto *types = (unsigned char *)realloc(p.types, capacity);
  if (!types)
    return false;
  p.types = types;
  p.capacity = capacity;
  return true;
}
bool AddPathPoint(Context &dc, int x, int y, unsigned char type) {
  if (!ReservePath(dc, 1))
    return false;
  auto &p = *dc.path;
  p.points[p.count] = {x, y};
  p.types[p.count] = type;
  ++p.count;
  return true;
}
// BEGINPATH drops whatever the DC still carries and takes a fresh
// object; ABORTPATH drops it and clears the open bit.
bool BeginPathObject(Context &dc) {
  ReleasePath(dc);
  dc.path_flags &= ~(Win98GdiPath::Open | Win98GdiPath::Curved);
  if (!ReservePath(dc, 1))
    return false;
  dc.path_flags |= Win98GdiPath::Open;
  return true;
}
// Consumers flatten the path while the Curved bit is set. Each Bezier run
// becomes the corresponding sequence of line segments.
bool FlattenStoredPath(Context &dc) {
  if (!dc.path || !(dc.path_flags & Win98GdiPath::Curved))
    return true;
  auto &p = *dc.path;
  auto *points = (Win98GdiPath::Point *)calloc(p.count ? p.count : 1, sizeof(Win98GdiPath::Point));
  auto *types = (unsigned char *)calloc(p.count ? p.count : 1, 1);
  if (!points || !types) {
    free(points);
    free(types);
    return false;
  }
  const unsigned source_count = p.count;
  for (unsigned i = 0; i < source_count; ++i) {
    points[i] = p.points[i];
    types[i] = p.types[i];
  }
  p.count = 0;
  bool ok = true;
  Win98GdiPath::Point previous{0, 0};
  for (unsigned i = 0; ok && i < source_count;) {
    const unsigned char type = types[i] & 6;
    if (type == Win98GdiPath::BezierToType && i >= 1 && i + 2 < source_count) {
      const Win98GdiPath::Point curve[4] = {previous, points[i], points[i + 1],
                                            points[i + 2]};
      const unsigned char last = types[i + 2];
      ok = Win98GdiPath::Curve(curve, [&](Win98GdiPath::Point q) {
        return AddPathPoint(dc, q.x, q.y, Win98GdiPath::LineToType);
      });
      if (ok && p.count) {
        p.types[p.count - 1] = (unsigned char)(Win98GdiPath::LineToType |
                                               (last & Win98GdiPath::CloseFigureType));
        previous = points[i + 2];
      }
      i += 3;
      continue;
    }
    ok = AddPathPoint(dc, points[i].x, points[i].y, types[i]);
    previous = points[i];
    ++i;
  }
  free(points);
  free(types);
  if (ok)
    dc.path_flags &= ~Win98GdiPath::Curved;
  return ok;
}
// The guard every consumer runs first: (FILLPATH),
// (FLATTENPATH), (PATHTOREGION), (SELECTCLIPPATH),
// (STROKEANDFILLPATH), (STROKEPATH) and (WIDENPATH) are the
// same three tests in the same order: a path must exist, it must be closed,
// and a curved path is flattened before anything else happens.
bool UsablePath(Context &dc) {
  if (!dc.path || !dc.path->count || (dc.path_flags & Win98GdiPath::Open))
    return false;
  return FlattenStoredPath(dc);
}
// Splits the stored path into its figures. A figure starts at a PT_MOVETO and
// ends before the next one; the fill and the region close every figure, the
// stroke only closes the ones that carry PT_CLOSEFIGURE.
unsigned PathFigures(const GdiPath &p, unsigned *starts, unsigned *counts,
                     unsigned capacity) {
  unsigned figures = 0;
  for (unsigned i = 0; i < p.count; ++i) {
    if ((p.types[i] & 6) == Win98GdiPath::MoveToType || !figures) {
      if (figures == capacity)
        return figures;
      starts[figures] = i;
      counts[figures] = 0;
      ++figures;
    }
    ++counts[figures - 1];
  }
  return figures;
}
// Both painting entries map the stored logical points once. fills with
// the DC's brush through the polygon scan converter, strokes with the
// DC's pen.
bool PaintPath(Context &dc, BitmapIO &io, bool fill, bool stroke) {
  auto &p = *dc.path;
  const unsigned room = p.count * 2 + 2;
  auto *starts = (unsigned *)calloc(room, sizeof(unsigned));
  auto *counts = (unsigned *)calloc(room, sizeof(unsigned));
  auto *device = (Win98GdiShapes::Point *)calloc(room, sizeof(Win98GdiShapes::Point));
  auto *closed = (Win98GdiShapes::Point *)calloc(room, sizeof(Win98GdiShapes::Point));
  auto *sizes = (unsigned *)calloc(room, sizeof(unsigned));
  auto *edges = (Win98GdiShapes::Edge *)calloc(room, sizeof(Win98GdiShapes::Edge));
  bool ok = starts && counts && device && closed && sizes && edges;
  if (!ok) {
    free(starts); free(counts); free(device); free(closed); free(sizes); free(edges);
    io.Error(8);
    return false;
  }
  const unsigned figures = PathFigures(p, starts, counts, room);
  for (unsigned i = 0; i < p.count; ++i)
    device[i] = ShapePoint(dc.current, unsigned(p.points[i].x), unsigned(p.points[i].y));
  // Every figure of the path counts as closed for the fill, and the closing
  // point is appended before the scan conversion, exactly as GDI32
  // does for POLYGON.
  unsigned used = 0, contours = 0;
  auto *offsets = (unsigned *)calloc(room, sizeof(unsigned));
  if (!offsets) {
    free(starts); free(counts); free(device); free(closed); free(sizes); free(edges);
    io.Error(8);
    return false;
  }
  for (unsigned f = 0; f < figures; ++f) {
    if (counts[f] < 2)
      continue;
    offsets[contours] = used;
    for (unsigned i = 0; i < counts[f]; ++i)
      closed[used++] = device[starts[f] + i];
    unsigned n = counts[f];
    const auto &first = closed[offsets[contours]], &last = closed[used - 1];
    if (first.x != last.x || first.y != last.y) {
      closed[used++] = closed[offsets[contours]];
      ++n;
    }
    sizes[contours++] = n;
  }
  ShapeSink sink{dc, io};
  if (!sink.Prepare(fill))
    ok = false;
  if (ok && fill && sink.fill && contours) {
    unsigned edge_count = 0;
    int bottom = -32768;
    for (unsigned i = 0; i < used; ++i)
      if (closed[i].y > bottom)
        bottom = closed[i].y;
    for (unsigned f = 0; f < contours; ++f)
      edge_count += Win98GdiShapes::BuildEdges(edges + edge_count, closed,
                                               offsets[f], sizes[f]);
    if (edge_count) {
      BitmapAccess access(Find(dc.current.bitmap, BitmapObject), true);
      ok = Win98GdiShapes::PolygonSpans(sink, edges, edge_count, closed, bottom,
                                        dc.current.poly_fill_mode);
    }
  }
  // strokes with the DC's pen. Only a figure that carries
  // PT_CLOSEFIGURE gets its closing segment.
  if (ok && stroke)
    for (unsigned f = 0; ok && f < figures; ++f) {
      if (counts[f] < 2)
        continue;
      auto *line = (Win98PenLine::Point *)calloc(counts[f] + 1, sizeof(Win98PenLine::Point));
      if (!line) {
        ok = false;
        break;
      }
      unsigned n = 0;
      for (unsigned i = 0; i < counts[f]; ++i)
        line[n++] = {device[starts[f] + i].x, device[starts[f] + i].y};
      if ((p.types[starts[f] + counts[f] - 1] & Win98GdiPath::CloseFigureType) &&
          (line[n - 1].x != line[0].x || line[n - 1].y != line[0].y))
        line[n++] = line[0];
      ok = DrawPenPolyline(dc, line, n, io);
      free(line);
    }
  free(starts); free(counts); free(device); free(closed); free(sizes);
  free(edges); free(offsets);
  return ok;
}
// with DX=1: the same scan conversion, but the spans go into a region
// instead of onto the surface. PATHTOREGION and SELECTCLIPPATH share it.
unsigned PathRegion(Context &dc, BitmapIO &io) {
  auto &p = *dc.path;
  auto *device = (Win98GdiRegionShape::Point *)calloc(p.count, sizeof(Win98GdiRegionShape::Point));
  auto *starts = (unsigned *)calloc(p.count, sizeof(unsigned));
  auto *counts = (unsigned *)calloc(p.count, sizeof(unsigned));
  unsigned region = 0;
  if (device && starts && counts) {
    for (unsigned i = 0; i < p.count; ++i) {
      const auto q = ShapePoint(dc.current, unsigned(p.points[i].x), unsigned(p.points[i].y));
      device[i] = {q.x, q.y};
    }
    const unsigned figures = PathFigures(p, starts, counts, p.count);
    region = Win98RegionApi::Create(region_store, region_store.state, 0, 0, 0, 0);
    for (unsigned f = 0; region && f < figures; ++f) {
      if (counts[f] < 2)
        continue;
      unsigned figure = Win98RegionApi::Create(region_store, region_store.state, 0, 0, 0, 0);
      if (!figure) {
        region_store.Destroy(region);
        region = 0;
        break;
      }
      if (!Win98GdiRegionShape::Spans(
              device + starts[f], counts[f], dc.current.poly_fill_mode,
              [&](int y, int left, int right) {
                const unsigned span = Win98RegionApi::Create(
                    region_store, region_store.state, unsigned(left), unsigned(y),
                    unsigned(right), unsigned(y + 1));
                if (!span)
                  return false;
                Win98RegionApi::Combine(region_store, region_store.state, figure,
                                        figure, span, 2);
                region_store.Destroy(span);
                return true;
              })) {
        region_store.Destroy(figure);
        region_store.Destroy(region);
        region = 0;
        break;
      }
      Win98RegionApi::Combine(region_store, region_store.state, region, region, figure, 2);
      region_store.Destroy(figure);
    }
  }
  free(device);
  free(starts);
  free(counts);
  if (!region)
    io.Error(8);
  return region;
}
} // namespace

// Drawing entries check whether the DC has an open path before drawing.
// Line and polygon operations record their points, MOVETO records a move,
// and Bezier operations record curves. Other operations are not recordable;
// RECTANGLE returns FALSE while a path is open.
bool PathRecording(HDC handle) {
  auto *dc = DC(handle);
  return dc && (dc->path_flags & Win98GdiPath::Open);
}
bool PathRecord(ApiName name, const unsigned *a, BitmapIO &io, unsigned &result) {
  auto *c = DC(a[0]);
  if (!c || !(c->path_flags & Win98GdiPath::Open))
    return false;
  auto &dc = *c;
  result = 0;
  auto moved = [&](int x, int y) {
    dc.current.position.x = x;
    dc.current.position.y = y;
  };
  // a MoveTo in a path only moves the current point. The figure it
  // starts appears when the first line arrives.
  auto start_figure = [&]() {
    return AddPathPoint(dc, dc.current.position.x, dc.current.position.y,
                        Win98GdiPath::MoveToType);
  };
  auto continues = [&]() {
    // A line that follows another line stays in the same figure.
    return dc.path && dc.path->count &&
           (dc.path->types[dc.path->count - 1] & Win98GdiPath::CloseFigureType) == 0;
  };
  auto read_points = [&](unsigned pointer, unsigned count,
                         Win98GdiPath::Point *out) {
    for (unsigned i = 0; i < count; ++i) {
      unsigned pair[2];
      if (!io.Read(pointer + i * 8, pair, sizeof pair))
        return false;
      out[i] = {int(pair[0]), int(pair[1])};
    }
    return true;
  };
  switch (name.id) {
  API_CASE("MoveToEx") {
    if (a[3] && (!io.Probe(a[3], true) || !io.Probe(a[3] + 7, true))) {
      io.Error(87);
      return true;
    }
    const int before[] = {dc.current.position.x, dc.current.position.y};
    moved(int(a[1]), int(a[2]));
    if (a[3] && !io.Write(a[3], before, sizeof before))
      return true;
    result = 1;
    return true;
  }
  API_CASE("LineTo") {
    if (!continues() && !start_figure())
      return true;
    if (!AddPathPoint(dc, int(a[1]), int(a[2]), Win98GdiPath::LineToType))
      return true;
    moved(int(a[1]), int(a[2]));
    result = 1;
    return true;
  }
  API_CASE("CloseFigure")
  API_CASE("BeginPath")
  API_CASE("EndPath")
  API_CASE("AbortPath")
  API_CASE("SetArcDirection")
  API_CASE("GetArcDirection")
  API_CASE("GetPath")
    return false; // the family itself is never a record
  default:
    break;
  }
  const bool polyline = API_IS(name, "Polyline"), polyline_to = API_IS(name, "PolylineTo");
  const bool bezier = API_IS(name, "PolyBezier"), bezier_to = API_IS(name, "PolyBezierTo");
  const bool polygon = API_IS(name, "Polygon"), poly_polygon = API_IS(name, "PolyPolygon");
  const bool poly_polyline = API_IS(name, "PolyPolyline");
  if (!polyline && !polyline_to && !bezier && !bezier_to && !polygon &&
      !poly_polygon && !poly_polyline) {
    // RECTANGLE and its neighbours answer FALSE without recording
    // (ELLIPSE, ARC, PIE, CHORD, ROUNDRECT: the worker Windows 98 tests the bit
    // ), on every kind of DC. RECTANGLE compares the WORD
    // coordinates first : an empty rectangle answers TRUE
    // before the test.
    if (API_IS(name, "Rectangle") && ((a[1] & 65535) == (a[3] & 65535) ||
                                      (a[2] & 65535) == (a[4] & 65535))) {
      result = 1;
      return true;
    }
    switch (name.id) {
    API_CASE("Rectangle") API_CASE("Ellipse") API_CASE("RoundRect") API_CASE("Arc")
    API_CASE("Chord") API_CASE("Pie") API_CASE("ArcTo") API_CASE("AngleArc")
    API_CASE("TextOutA") API_CASE("ExtTextOutA") API_CASE("TextOutW")
    API_CASE("ExtTextOutW")
      return true; // result stays zero
    default:
      return false;
    }
  }
  if (poly_polygon || poly_polyline) {
    const unsigned contours = a[3];
    if (!contours || contours > 0x3ffd) {
      io.Error(87);
      return true;
    }
    auto *sizes = (unsigned *)calloc(contours, sizeof(unsigned));
    if (!sizes) {
      io.Error(8);
      return true;
    }
    unsigned total = 0;
    bool ok = true;
    for (unsigned i = 0; ok && i < contours; ++i) {
      if (!io.Read(a[2] + i * 4, sizes + i, 4) || sizes[i] < 2 || sizes[i] > 0x3fff)
        ok = false;
      else
        total += sizes[i];
    }
    auto *points = ok ? (Win98GdiPath::Point *)calloc(total, sizeof(Win98GdiPath::Point)) : nullptr;
    if (ok && (!points || !read_points(a[1], total, points)))
      ok = false;
    unsigned base = 0;
    for (unsigned i = 0; ok && i < contours; ++i) {
      ok = AddPathPoint(dc, points[base].x, points[base].y, Win98GdiPath::MoveToType);
      for (unsigned j = 1; ok && j < sizes[i]; ++j)
        ok = AddPathPoint(dc, points[base + j].x, points[base + j].y,
                          Win98GdiPath::LineToType);
      if (ok && poly_polygon && dc.path->count)
        dc.path->types[dc.path->count - 1] |= Win98GdiPath::CloseFigureType;
      if (ok)
        moved(points[base + sizes[i] - 1].x, points[base + sizes[i] - 1].y);
      base += sizes[i];
    }
    free(sizes);
    free(points);
    result = ok;
    if (!ok)
      io.Error(8);
    return true;
  }
  unsigned count = a[2] & 65535;
  if (bezier || bezier_to) {
    // PolyBezier needs 3n+1 points, PolyBezierTo 3n.
    const unsigned rest = bezier ? (count ? count - 1 : 0) : count;
    if ((count & 0xc000) || !rest || rest % 3) {
      io.Error(87);
      return true;
    }
  } else if (count < 2 || (count & 0xc000)) {
    io.Error(87);
    return true;
  }
  auto *points = (Win98GdiPath::Point *)calloc(count, sizeof(Win98GdiPath::Point));
  if (!points || !read_points(a[1], count, points)) {
    free(points);
    io.Error(points ? 87 : 8);
    return true;
  }
  bool ok = true;
  const bool absolute = polyline || polygon || bezier;
  if (absolute) {
    ok = AddPathPoint(dc, points[0].x, points[0].y, Win98GdiPath::MoveToType);
  } else if (!continues()) {
    ok = start_figure();
  }
  const unsigned char type = (bezier || bezier_to) ? Win98GdiPath::BezierToType
                                                   : Win98GdiPath::LineToType;
  for (unsigned i = absolute ? 1 : 0; ok && i < count; ++i)
    ok = AddPathPoint(dc, points[i].x, points[i].y, type);
  if (ok && polygon && dc.path->count)
    dc.path->types[dc.path->count - 1] |= Win98GdiPath::CloseFigureType;
  if (ok) {
    moved(points[count - 1].x, points[count - 1].y);
    if (bezier || bezier_to)
      dc.path_flags |= Win98GdiPath::Curved;
  }
  free(points);
  result = ok;
  if (!ok)
    io.Error(8);
  return true;
}

bool IsPathApi(ApiName name) { return PathIds.Has(name.id); }
unsigned PathApi(ApiName name, const unsigned *a, BitmapIO &io) {
  RegionSerial serial;
  auto *c = DC(a[0]);
  if (!c) {
    io.Error(6);
    return API_IS(name, "GetPath") ? ~0u : 0;
  }
  auto &dc = *c;
  switch (name.id) {
  // and only read and write bit 2 of DC+7Ah.
  API_CASE("GetArcDirection") return Win98GdiPath::ArcDirection(dc.current.arc_flags);
  API_CASE("SetArcDirection") {
    unsigned previous = 0;
    if (!Win98GdiPath::SetArcDirection(dc.current.arc_flags, a[1], previous)) {
      io.Error(87);
      return 0;
    }
    return previous;
  }
  // BEGINPATH.
  API_CASE("BeginPath") {
    if (!BeginPathObject(dc)) {
      io.Error(8);
      return 0;
    }
    return 1;
  }
  // ENDPATH clears the open bit and answers FALSE when none was set.
  API_CASE("EndPath") {
    if (!(dc.path_flags & Win98GdiPath::Open))
      return 0;
    dc.path_flags &= ~Win98GdiPath::Open;
    return 1;
  }
  // ABORTPATH always answers TRUE, open path or not.
  API_CASE("AbortPath") {
    ReleasePath(dc);
    dc.path_flags &= ~(Win98GdiPath::Open | Win98GdiPath::Curved);
    return 1;
  }
  // CLOSEFIGURE needs an open path and marks the last point.
  API_CASE("CloseFigure") {
    if (!(dc.path_flags & Win98GdiPath::Open) || !dc.path || !dc.path->count)
      return 0;
    dc.path->types[dc.path->count - 1] |= Win98GdiPath::CloseFigureType;
    return 1;
  }
  // GETPATH. Without a buffer the point count, otherwise the points
  // and their types; a buffer smaller than the path answers -1.
  API_CASE("GetPath") {
    if (!dc.path || !dc.path->count || (dc.path_flags & Win98GdiPath::Open))
      return ~0u;
    const unsigned count = dc.path->count;
    if (!int(a[3]))
      return count;
    if (!a[1] || !a[2] || int(a[3]) < 0 || unsigned(a[3]) < count)
      return ~0u;
    for (unsigned i = 0; i < count; ++i) {
      const int pair[2] = {dc.path->points[i].x, dc.path->points[i].y};
      if (!io.Write(a[1] + i * 8, pair, sizeof pair) ||
          !io.Write(a[2] + i, dc.path->types + i, 1))
        return ~0u;
    }
    return count;
  }
  // FLATTENPATH turns the Bezier records into line records.
  API_CASE("FlattenPath") {
    if (!UsablePath(dc))
      return 0;
    return 1;
  }
  API_CASE("FillPath") API_CASE("StrokePath") API_CASE("StrokeAndFillPath") {
    if (!UsablePath(dc))
      return 0;
    if (!Win98DcInformation::DrawingAllowed(dc.signature)) {
      ReleasePath(dc);
      return 1;
    }
    const bool fill = !API_IS(name, "StrokePath");
    const bool stroke = !API_IS(name, "FillPath");
    const bool ok = PaintPath(dc, io, fill, stroke);
    // All three operations drop the path afterwards.
    ReleasePath(dc);
    return ok;
  }
  // PATHTOREGION hands the region out and drops the path.
  API_CASE("PathToRegion") {
    if (!UsablePath(dc))
      return 0;
    if (!region_store.Initialize()) {
      io.Error(8);
      return 0;
    }
    const unsigned region = PathRegion(dc, io);
    ReleasePath(dc);
    return region ? PublishRegion(region) : 0;
  }
  // Windows 98 validates the mode against 1..5 before Windows 98 builds the region
  // and combines it into the application clip.
  API_CASE("SelectClipPath") {
    if (a[1] < 1 || a[1] > 5) {
      io.Error(87);
      return 0;
    }
    if (!UsablePath(dc))
      return 0;
    if (!region_store.Initialize()) {
      io.Error(8);
      return 0;
    }
    unsigned region = PathRegion(dc, io);
    ReleasePath(dc);
    if (!region)
      return 0;
    if (a[1] != 5) {
      unsigned previous = CopyClip(dc.current, true);
      if (!previous) {
        region_store.Destroy(region);
        return 0;
      }
      unsigned combined = Win98RegionApi::Combine(region_store, region_store.state,
                                                  region, previous, region, a[1]);
      region_store.Destroy(previous);
      if (!combined) {
        region_store.Destroy(region);
        return 0;
      }
    }
    return ReplaceClip(dc.current, region, true) ? EffectiveClipType(dc.current) : 0;
  }
  // Widen even a one-pixel pen into a closed, fillable stroke outline.
  API_CASE("WidenPath") {
    if (!UsablePath(dc))
      return 0;
    auto *pen = Find(dc.current.pen, PenObject);
    if (!pen)
      return 0;
    int width = Win98PenLine::Word(pen->pen.width_x);
    if (width < 0) width = -width;
    if (!width) return 0;
    float limit; memcpy(&limit, &dc.current.miter_limit, 4);
    const auto &source = *dc.path;
    auto *figure = (Win98GdiPath::Point *)calloc(source.count, sizeof(Win98GdiPath::Point));
    if (!figure) { io.Error(8); return 0; }
    Context output;
    auto emit = [&](Win98GdiPath::Point p, bool start) {
      if (start && output.path && output.path->count)
        output.path->types[output.path->count-1] |= Win98GdiPath::CloseFigureType;
      return AddPathPoint(output,p.x,p.y,start?Win98GdiPath::MoveToType:Win98GdiPath::LineToType);
    };
    bool ok = true;
    for (unsigned at=0;ok && at<source.count;) {
      unsigned end=at+1;
      while(end<source.count && (source.types[end]&6)!=Win98GdiPath::MoveToType &&
            !(source.types[end-1]&Win98GdiPath::CloseFigureType)) ++end;
      unsigned count=0;
      for(unsigned i=at;i<end;++i)
        if(!count || source.points[i].x!=figure[count-1].x || source.points[i].y!=figure[count-1].y)
          figure[count++]=source.points[i];
      const bool closed=(source.types[end-1]&Win98GdiPath::CloseFigureType)!=0;
      ok=Win98GdiWiden::Figure(figure,count,closed,width*.5,pen->pen.flags,limit,emit);
      at=end;
    }
    free(figure);
    if (!ok) { ReleasePath(output); io.Error(8); return 0; }
    if(output.path && output.path->count)
      output.path->types[output.path->count-1] |= Win98GdiPath::CloseFigureType;
    ReleasePath(dc); dc.path=output.path;
    dc.path_flags &= ~(Win98GdiPath::Open|Win98GdiPath::Curved);
    return 1;
  }
  // The three curve and polyline entries of the collection, drawn rather than
  // recorded. and Windows 98 validate the point count, then segment 32
  // flattens; POLYPOLYLINE has no own GDI.EXE export and draws one POLYLINE
  // per contour.
  API_CASE("PolyBezier") API_CASE("PolyBezierTo") {
    const unsigned count = a[2] & 65535;
    const bool to = API_IS(name, "PolyBezierTo");
    const unsigned rest = to ? count : (count ? count - 1 : 0);
    if ((count & 0xc000) || !rest || rest % 3) {
      io.Error(87);
      return 0;
    }
    auto *points = (Win98GdiPath::Point *)calloc(count + 1, sizeof(Win98GdiPath::Point));
    if (!points) {
      io.Error(8);
      return 0;
    }
    unsigned first = 0;
    if (to)
      points[first++] = {dc.current.position.x, dc.current.position.y};
    bool ok = true;
    for (unsigned i = 0; ok && i < count; ++i) {
      unsigned pair[2];
      if (!io.Read(a[1] + i * 8, pair, sizeof pair))
        ok = false;
      else
        points[first + i] = {int(pair[0]), int(pair[1])};
    }
    const unsigned total = first + count;
    // Each cubic starts at the previous end point.
    for (unsigned base = 0; ok && base + 3 < total; base += 3) {
      Win98PenLine::Point line[2];
      Win98GdiPath::Point previous = points[base];
      ok = Win98GdiPath::Curve(points + base, [&](Win98GdiPath::Point q) {
        line[0] = PenDevicePoint(dc.current, unsigned(previous.x), unsigned(previous.y));
        line[1] = PenDevicePoint(dc.current, unsigned(q.x), unsigned(q.y));
        previous = q;
        return DrawPenPolyline(dc, line, 2, io);
      });
    }
    if (ok && total) {
      dc.current.position.x = points[total - 1].x;
      dc.current.position.y = points[total - 1].y;
    }
    free(points);
    return ok;
  }
  API_CASE("PolylineTo") {
    const unsigned count = a[2] & 65535;
    if (!count || (count & 0xc000)) {
      io.Error(87);
      return 0;
    }
    auto *points = (Win98PenLine::Point *)calloc(count + 1, sizeof(Win98PenLine::Point));
    if (!points) {
      io.Error(8);
      return 0;
    }
    bool ok = true;
    int last_x = dc.current.position.x, last_y = dc.current.position.y;
    points[0] = PenDevicePoint(dc.current, unsigned(last_x), unsigned(last_y));
    for (unsigned i = 0; ok && i < count; ++i) {
      unsigned pair[2];
      if (!io.Read(a[1] + i * 8, pair, sizeof pair)) {
        io.Error(87);
        ok = false;
        break;
      }
      last_x = int(pair[0]);
      last_y = int(pair[1]);
      points[i + 1] = PenDevicePoint(dc.current, pair[0], pair[1]);
    }
    if (ok)
      ok = DrawPenPolyline(dc, points, count + 1, io);
    if (ok) {
      dc.current.position.x = last_x;
      dc.current.position.y = last_y;
    }
    free(points);
    return ok;
  }
  API_CASE("PolyPolyline") {
    const unsigned contours = a[3];
    if (!contours || contours > 0x3ffd) {
      io.Error(87);
      return 0;
    }
    auto *sizes = (unsigned *)calloc(contours, sizeof(unsigned));
    if (!sizes) {
      io.Error(8);
      return 0;
    }
    bool ok = true;
    unsigned total = 0;
    for (unsigned i = 0; ok && i < contours; ++i) {
      if (!io.Read(a[2] + i * 4, sizes + i, 4) || sizes[i] < 2 || sizes[i] > 0x3fff) {
        io.Error(87);
        ok = false;
      } else
        total += sizes[i];
    }
    auto *points = ok ? (Win98PenLine::Point *)calloc(total, sizeof(Win98PenLine::Point)) : nullptr;
    if (ok && !points) {
      io.Error(8);
      ok = false;
    }
    for (unsigned i = 0; ok && i < total; ++i) {
      unsigned pair[2];
      if (!io.Read(a[1] + i * 8, pair, sizeof pair)) {
        io.Error(87);
        ok = false;
      } else
        points[i] = PenDevicePoint(dc.current, pair[0], pair[1]);
    }
    for (unsigned i = 0, base = 0; ok && i < contours; base += sizes[i], ++i)
      ok = DrawPenPolyline(dc, points + base, sizes[i], io);
    free(sizes);
    free(points);
    return ok;
  }
  // EXTFLOODFILL rejects a fill type above one with error E001h and
  // reaches the scan line filler. FLOODFILL is the same
  // call with FLOODFILLBORDER.
  API_CASE("FloodFill") API_CASE("ExtFloodFill") {
    const bool extended = API_IS(name, "ExtFloodFill");
    const unsigned type = extended ? a[4] : 0;
    if (type > 1) {
      io.Error(87);
      return 0;
    }
    if (!Win98DcInformation::DrawingAllowed(dc.signature))
      return 0;
    auto *bitmap = Find(dc.current.bitmap, BitmapObject);
    auto *brush = Find(dc.current.brush, BrushObject);
    if (!bitmap || !brush) {
      io.Error(87);
      return 0;
    }
    unsigned reference = 0;
    if (!RealizeDeviceColor(*bitmap, a[3], reference, &dc.current)) {
      io.Error(50);
      return 0;
    }
    DibEng::Brush realized{};
    unsigned pattern[64]{};
    if (brush->hollow_brush)
      return 0;
    if (!RealizeBrush(*brush, dc.current, pattern, realized)) {
      io.Error(50);
      return 0;
    }
    const auto seed = PenDevicePoint(dc.current, a[1], a[2]);
    BitmapAccess access(bitmap, true);
    // A 32 bpp DIB carries eight bits the driver never realizes; comparing
    // them would make the fill depend on an undefined byte, so the test uses
    // the bits the depth really has.
    const unsigned depth = unsigned(bitmap->bitmap.depth);
    const unsigned mask = depth >= 24 ? 0xffffffu : (1u << depth) - 1;
    reference &= mask;
    const size_t width = size_t(bitmap->bitmap.width);
    const size_t height = size_t(bitmap->bitmap.height);
    if (!width || !height || width > SIZE_MAX / height) {
      io.Error(8);
      return 0;
    }
    unsigned char *visited = (unsigned char *)calloc(width * height, 1);
    if (!visited) {
      io.Error(8);
      return 0;
    }
    struct Surface {
      Context &dc;
      DibEng::Bitmap &bitmap;
      const DibEng::Brush &brush;
      unsigned mask;
      unsigned char *visited;
      bool Inside(int x, int y) const {
        if (x < 0 || y < 0 || x >= bitmap.width || y >= bitmap.height)
          return false;
        for (unsigned i = 0; i < dc.current.clips; ++i) {
          const auto &c = ClipAt(dc.current, i);
          if (x >= c.left && x < c.right && y >= c.top && y < c.bottom)
            return true;
        }
        return false;
      }
      unsigned Read(int x, int y) const { return DibEng::ReadPixel(bitmap, x, y) & mask; }
      bool Seen(int x, int y) const { return visited[size_t(y) * size_t(bitmap.width) + size_t(x)]; }
      void Mark(int x, int y) { visited[size_t(y) * size_t(bitmap.width) + size_t(x)] = 1; }
      void Write(int x, int y) {
        const unsigned source =
            brush.pattern ? brush.pattern[(unsigned(y) & 7) * 8 + (unsigned(x) & 7)]
                          : brush.color;
        DibEng::WritePixel(bitmap, x, y,
                           Win98GdiPixel::Rop(dc.current.text_mode.rop2, source,
                                              DibEng::ReadPixel(bitmap, x, y)));
      }
    } surface{dc, bitmap->bitmap, realized, mask, visited};
    bool exhausted = false;
    const bool filled = Win98GdiFlood::Fill(surface, seed.x, seed.y, reference, type == 1,
                                            &exhausted);
    free(visited);
    if (exhausted)
      io.Error(8);
    return filled;
  }
  default:
    break;
  }
  return 0;
}
