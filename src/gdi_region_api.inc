/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
namespace {
constexpr const char *RegionNames[] = {
    "CreateRectRgn",    "CreateRectRgnIndirect",
    "SetRectRgn",       "CombineRgn",
    "OffsetRgn",        "GetRgnBox",
    "PtInRegion",       "RectInRegion",
    "EqualRgn",         "GetRegionData",
    "ExtCreateRegion",  "SelectClipRgn",
    "ExtSelectClipRgn", "GetClipRgn",
    "OffsetClipRgn",    "CreatePolygonRgn",
    "GetRandomRgn",     "CreatePolyPolygonRgn"};
bool RegionRange(BitmapIO &io, unsigned p, unsigned bytes, bool write) {
  if (!p || !bytes || (unsigned long long)p + bytes > 0x100000000ull)
    return false;
  for (unsigned off = 0; off < bytes;) {
    if (!io.Probe(p + off, write))
      return false;
    unsigned next = 4096 - ((p + off) & 4095);
    if (next >= bytes - off)
      break;
    off += next;
  }
  return io.Probe(p + bytes - 1, write);
}
} // namespace
constexpr ApiIdTable RegionIds(RegionNames);
bool IsRegionApi(ApiName name) { return RegionIds.Has(name.id); }
unsigned RegionApi(ApiName name, const unsigned *a, BitmapIO &io) {
  const unsigned op = RegionIds.Index(name.id);
  if (op >= 18)
    return 0;
  RegionSerial serial;
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
  if (!region_store.Initialize()) {
    io.Error(8);
    return 0;
  }
  auto &e = region_store;
  auto &s = e.state;
  unsigned raw = RawRegion(a[0]);
  if (op == 0 || op == 1) {
    unsigned r[4];
    if (op == 0)
      memcpy(r, a, sizeof r);
    else if (!io.Read(a[0], r, sizeof r))
      return 0;
    return PublishRegion(Win98RegionApi::Create(e, s, r[0], r[1], r[2], r[3]));
  }
  if (op == 2)
    return raw ? Win98RegionApi::Set(e, s, raw, a[1], a[2], a[3], a[4]) : 0;
  if (op == 3)
    return Win98RegionApi::Combine(e, s, raw, RawRegion(a[1]), RawRegion(a[2]),
                                   a[3]);
  if (op == 4)
    return Win98RegionApi::Offset(e, raw, a[1], a[2]);
  if (op == 5) {
    if (!raw || !RegionRange(io, a[1], 16, true))
      return 0;
    auto r = Win98Region::Bounds(e, raw);
    int out[] = {Win98Region::S(r.left), Win98Region::S(r.top),
                 Win98Region::S(r.right), Win98Region::S(r.bottom)};
    return io.Write(a[1], out, 16) ? Win98Region::Complexity(e, raw) : 0;
  }
  if (op == 6)
    return Win98RegionApi::Point(e, raw, a[1], a[2]);
  if (op == 7) {
    unsigned r[4];
    if (!raw || !io.Read(a[1], r, 16))
      return 0;
    return Win98RegionApi::ContainsRect(
        e, raw, {r[0] & 65535, r[1] & 65535, r[2] & 65535, r[3] & 65535},
        io.ExpectedVersion());
  }
  if (op == 8)
    return Win98RegionApi::Equal(e, raw, RawRegion(a[1]));
  if (op == 9) {
    if (!raw)
      return 0;
    unsigned bytes = Win98Region::Data(e, raw, 0, 0);
    if (!a[2] || a[1] < bytes)
      return bytes;
    if (!RegionRange(io, a[2], bytes, true))
      return 0;
    const unsigned map = bitmap_store.Map(nullptr, bytes, &io, a[2]);
    if (!map) {
      io.Error(8);
      return 0;
    }
    unsigned result =
        Win98RegionApi::Data(e, raw, a[1], bitmap_store.ranges[map - 1].base);
    bitmap_store.Unmap(map);
    return bitmap_store.failed ? 0 : result;
  }
  if (op == 10) {
    unsigned header[8];
    // Win98 reads the header and rectangles independently of the cbRgnData
    // argument. Probe the actual input rather than trusting that byte count.
    if (!io.Read(a[2], header, 32) || header[0] < 32 || header[1] != 1 ||
        header[2] > (RegionStore::Limit - 32) / 16)
      return 0;
    unsigned bytes = 32 + header[2] * 16;
    if (!RegionRange(io, a[2], bytes, false))
      return 0;
    if (a[0]) {
      float transform[6];
      if (!io.Read(a[0], transform, sizeof transform)) return 0;
      for (float value : transform)
        if (!__builtin_isfinite(value)) return 0;
      const unsigned region = Win98RegionApi::Create(e, s, 0, 0, 0, 0);
      if (!region) return 0;
      bool valid = true;
      for (unsigned i = 0; i < header[2] && valid; ++i) {
        int rect[4];
        if (!io.Read(a[2] + 32 + i * 16, rect, sizeof rect)) { valid = false; break; }
        if (rect[0] >= rect[2] || rect[1] >= rect[3]) continue;
        Win98GdiRegionShape::Point points[4];
        for (unsigned j = 0; j < 4; ++j) {
          const double x = rect[j == 1 || j == 2 ? 2 : 0];
          const double y = rect[j >= 2 ? 3 : 1];
          const double tx = x * transform[0] + y * transform[2] + transform[4];
          const double ty = x * transform[1] + y * transform[3] + transform[5];
          // Regions use signed 16-bit device coordinates in Win98 GDI.
          if (!(tx >= -32768 && tx < 32767.5 && ty >= -32768 && ty < 32767.5)) {
            valid = false; break;
          }
          points[j] = {int(__builtin_floor(tx + 0.5)), int(__builtin_floor(ty + 0.5))};
        }
        if (valid) valid = Win98GdiRegionShape::Spans(points, 4, 2, [&](int y, int left, int right) {
          const unsigned span = Win98RegionApi::Create(e, s, left, y, right, y + 1);
          if (!span) return false;
          const unsigned combined = Win98RegionApi::Combine(e, s, region, region, span, 2);
          e.Destroy(span);
          return combined != 0;
        });
      }
      if (!valid) { e.Destroy(region); return 0; }
      return PublishRegion(region);
    }
    const unsigned map = bitmap_store.Map(nullptr, bytes, &io, a[2]);
    if (!map) {
      io.Error(8);
      return 0;
    }
    unsigned result =
        Win98Region::Import(e, s, bitmap_store.ranges[map - 1].base);
    bitmap_store.Unmap(map);
    if (bitmap_store.failed) {
      e.Destroy(result);
      return 0;
    }
    return PublishRegion(result);
  }
  if (op == 15) {
    // CreatePolygonRgn(points, count, mode): the polygon scan-converted into
    // horizontal spans by win98_gdi_region_shape.h, each span OR-ed into the
    // region.
    const unsigned count = a[1];
    if (count < 2 || count > 1024 || (a[2] != 1 && a[2] != 2) || !RegionRange(io, a[0], count * 8, false))
      return 0;
    Win98GdiRegionShape::Point points[1024];
    if (!io.Read(a[0], points, count * 8))
      return 0;
    unsigned region = Win98RegionApi::Create(e, s, 0, 0, 0, 0);
    if (!region)
      return 0;
    if (!Win98GdiRegionShape::Spans(points, count, a[2], [&](int y, int left, int right) {
          const unsigned span = Win98RegionApi::Create(e, s, left, y, right, y + 1);
          if (!span)
            return false;
          Win98RegionApi::Combine(e, s, region, region, span, 2);
          e.Destroy(span);
          return true;
        })) {
      e.Destroy(region);
      return 0;
    }
    return PublishRegion(region);
  }
  if (op == 17) {
    // CreatePolyPolygonRgn(points, counts, polygons, mode), GDI32.
    // packs the call under SEH: the counts become words, the points
    // two shorts, and a polygon whose last point is not its first gets the
    // first once more. A fault on the way answers zero. GDI only
    // warns about a fill mode outside 1 and 2; Windows 98 answers zero for no
    // polygon; refuses more than 3FFDh points in all and fewer than
    // two; refuses any polygon under two points. fills with
    // the parity rule when bit 0 of the mode is set and with the winding rule
    // otherwise.
    const unsigned polygons = a[2];
    if (!polygons || polygons > 4096)
      return 0;
    unsigned counts[4096], total = 0;
    if (!io.Read(a[1], counts, polygons * 4))
      return 0;
    for (unsigned k = 0; k < polygons; ++k) {
      if (counts[k] > 0x3ffd || total + counts[k] > 0x3ffd)
        return 0;
      total += counts[k];
    }
    Win98GdiRegionShape::Point *points = new Win98GdiRegionShape::Point[total + polygons + 1];
    unsigned packed = 0, source = a[0];
    bool ok = true;
    for (unsigned k = 0; k < polygons && ok; ++k) {
      const unsigned first = packed;
      for (unsigned i = 0; i < counts[k]; ++i, source += 8) {
        int xy[2];
        if (!io.Read(source, xy, 8)) { ok = false; break; }
        points[packed++] = {short(xy[0]), short(xy[1])};
      }
      if (ok && counts[k] && (points[packed - 1].x != points[first].x || points[packed - 1].y != points[first].y)) {
        points[packed++] = points[first];
        ++counts[k];
      }
    }
    if (!ok || packed > 0x3ffd || packed < 2) {
      delete[] points;
      return 0;
    }
    unsigned region = Win98RegionApi::Create(e, s, 0, 0, 0, 0);
    if (!region) {
      delete[] points;
      return 0;
    }
    const bool filled = Win98GdiRegionShape::PolySpans(points, counts, polygons, (a[3] & 1) != 0,
        [&](int y, int left, int right) {
          const unsigned span = Win98RegionApi::Create(e, s, left, y, right, y + 1);
          if (!span)
            return false;
          Win98RegionApi::Combine(e, s, region, region, span, 2);
          e.Destroy(span);
          return true;
        });
    delete[] points;
    if (!filled) {
      e.Destroy(region);
      return 0;
    }
    return PublishRegion(region);
  }
  auto *dc = DC(a[0]);
  if (op == 16) {
    // GetRandomRgn(hdc, hrgn, iNum), GDI32; GetClipRgn is this routine
    // with iNum 1. iNum outside 1..4 answers 0 before anything is looked at.
    // 1 is the clip region of the application, 2 the meta region, 3 the
    // intersection of the two or the one that exists, 4 the visible region
    // of the DC. A region that does not exist answers 0, a bad handle -1 with
    // error 6, and the copy is moved by the DC origin like GetClipRgn's. The
    // meta region is the one SetMetaRgn built; 3 is the intersection
    // of clip and meta region when both exist. MSHTML asks for 4 while it
    // paints the folder web view; an export without a body stopped Explorer
    // there.
    const unsigned which = a[2];
    if (which < 1 || which > 4)
      return 0;
    const unsigned destination = RawRegion(a[1]);
    if (!destination || !dc) {
      io.Error(6);
      return ~0u;
    }
    const auto *app = dc->current.application_snapshot, *meta = dc->current.meta_snapshot;
    unsigned source = 0;
    if (which == 2 || which == 3) {
      const auto *first = which == 2 ? meta : app ? app : meta;
      if (!first)
        return 0;
      source = region_store.Clone(first->raw);
      if (source && which == 3 && app && meta &&
          !Win98Region::Combine(region_store, region_store.state, source, source, meta->raw, 1)) {
        e.Destroy(source);
        return ~0u;
      }
    } else {
      const bool application = which != 4;
      if (application && !app)
        return 0;
      source = CopyClip(dc->current, application);
    }
    if (!source)
      return ~0u;
    Win98Region::Offset(e, source,
                        unsigned(dc->current.surface_origin.x -
                                 dc->current.mapping.device_origin.x),
                        unsigned(dc->current.surface_origin.y -
                                 dc->current.mapping.device_origin.y));
    unsigned result = Win98RegionApi::Combine(e, s, destination, source, 0, 5);
    e.Destroy(source);
    return result ? 1 : ~0u;
  }
  if (op == 13) {
    const unsigned destination = RawRegion(a[1]);
    // GDI32..FCF rejects either signature with error6/-1.
    if (!destination || !dc) {
      io.Error(6);
      return ~0u;
    }
    if (!dc->current.application_snapshot)
      return 0;
    unsigned source = CopyClip(dc->current, true);
    if (!source)
      return ~0u;
    Win98Region::Offset(e, source,
                        unsigned(dc->current.surface_origin.x -
                                 dc->current.mapping.device_origin.x),
                        unsigned(dc->current.surface_origin.y -
                                 dc->current.mapping.device_origin.y));
    unsigned result = Win98RegionApi::Combine(e, s, destination, source, 0, 5);
    e.Destroy(source);
    return result ? 1 : ~0u;
  }
  if (!dc)
    return 0;
  if (op == 14) {
    unsigned copy = CopyClip(dc->current, true);
    if (!copy)
      return 0;
    const auto delta =
        Win98RegionClip::OffsetDelta(dc->current.mapping, a[1], a[2]);
    Win98Region::Offset(e, copy, unsigned(delta.x), unsigned(delta.y));
    return ReplaceClip(dc->current, copy, true) ? EffectiveClipType(dc->current)
                                                : 0;
  }
  unsigned mode = op == 11 ? 5 : (a[2] & 65535);
  if (mode < 1 || mode > 5)
    return 0;
  unsigned source = RawRegion(a[1]);
  if (a[1] && !source)
    return 0;
  if (!source) {
    if (mode != 5)
      return 0;
    return ReplaceClip(dc->current, 0, true) ? EffectiveClipType(dc->current)
                                             : 0;
  }

  unsigned copy = e.Clone(source);
  if (!copy)
    return 0;
  e.Write16(e.Pointer(copy) + 12, 0);
  Win98Region::Offset(e, copy,
                      unsigned(dc->current.mapping.device_origin.x -
                               dc->current.surface_origin.x),
                      unsigned(dc->current.mapping.device_origin.y -
                               dc->current.surface_origin.y));
  if (mode != 5) {
    unsigned previous = CopyClip(dc->current, true);
    if (!previous) {
      e.Destroy(copy);
      return 0;
    }
    unsigned result = Win98Region::Combine(e, s, copy, previous, copy, mode);
    e.Destroy(previous);
    if (!result) {
      e.Destroy(copy);
      return 0;
    }
  }
  return ReplaceClip(dc->current, copy, true) ? EffectiveClipType(dc->current)
                                              : 0;
}
