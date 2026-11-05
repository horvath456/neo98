/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Native ownership and checked-memory providers for the original region core.
struct RegionStore {
  // Region handles are WORDs; allocation sizes and mapped addresses are DWORDs.
  static constexpr unsigned MaxSlots=(0x10000-0x2000)/4, Limit=~0u;
  struct Block {
    unsigned range = 0, size = 0;
    unsigned char *bytes = nullptr;
  };
  Block *blocks=nullptr;unsigned capacity=0;
  ~RegionStore(){Shutdown();}
  Win98Region::State state{};
  unsigned Raw(unsigned index) const { return 0x2000 + index * 4; }
  Block *Record(unsigned h) {
    if (h < 0x2000 || (h - 0x2000) % 4)
      return nullptr;
    unsigned i = (h - 0x2000) / 4;
    return i < capacity && blocks[i].range ? &blocks[i] : nullptr;
  }
  unsigned Pointer(unsigned h) {
    auto *b = Record(h);
    return b ? bitmap_store.ranges[b->range - 1].base : 0;
  }
  unsigned Allocate(unsigned n) {
    if (!n)return 0;
    unsigned i=0;while(i<capacity&&blocks[i].range)++i;
    if(i==capacity){
      if(capacity==MaxSlots)return 0;
      const unsigned count=capacity?(capacity*2>MaxSlots?MaxSlots:capacity*2):64;
      auto *more=new(std::nothrow) Block[count];if(!more)return 0;
      for(unsigned k=0;k<capacity;++k)more[k]=blocks[k];
      delete[] blocks;blocks=more;capacity=count;
    }
    auto *p=static_cast<unsigned char *>(malloc(n));if(!p)return 0;
    memset(p,0,n);
    unsigned range=bitmap_store.Map(p,n);
    if(!range){free(p);return 0;}
    blocks[i]={range,n,p};return Raw(i);
  }
  unsigned Reallocate(unsigned h, unsigned n) {
    auto *b = Record(h);
    if (!b || !n || n > Limit)
      return 0;
    if (n <= b->size) {
      b->size = n;
      bitmap_store.ranges[b->range - 1].size = n;
      return h;
    }
    auto *p = static_cast<unsigned char *>(malloc(n));
    if (!p)
      return 0;
    memset(p, 0, n);
    memcpy(p, b->bytes, b->size < n ? b->size : n);
    // Obtain the new address before releasing the original mapping.
    const unsigned range = bitmap_store.Map(p, n);
    if (!range) {
      free(p);
      return 0;
    }
    bitmap_store.Unmap(b->range);
    free(b->bytes);
    *b = {range, n, p};
    return h;
  }
  unsigned Free(unsigned h) {
    auto *b = Record(h);
    if (!b)
      return h;
    bitmap_store.Unmap(b->range);
    free(b->bytes);
    *b = {};
    return 0;
  }
  unsigned Size(unsigned h) {
    auto *b = Record(h);
    return b ? b->size : 0;
  }
  unsigned Read8(unsigned p) { return bitmap_store.Read8(p); }
  unsigned Read16(unsigned p) { return bitmap_store.Read16(p); }
  unsigned Read32(unsigned p) { return bitmap_store.Read32(p); }
  void Write8(unsigned p, unsigned v) { bitmap_store.Write8(p, v); }
  void Write16(unsigned p, unsigned v) { bitmap_store.Write16(p, v); }
  void Write32(unsigned p, unsigned v) { bitmap_store.Write32(p, v); }
  void Move(unsigned d, unsigned s, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      Write8(d + i, Read8(s + i));
  }
  unsigned CallerOwner() {
    return 0;
  } // Native single application task identity.
  bool Initialize() {
    if (state.scratch_handle)
      return true;
    state.scratch_handle = Allocate(256);
    state.scratch_bytes = 256;
    return state.scratch_handle != 0;
  }
  unsigned Create(const Win98Region::Rect &r) {
    return Win98Region::Create(*this, state, r);
  }
  unsigned Clone(unsigned h) { return Win98Region::Clone(*this, state, h); }

  void Destroy(unsigned h) {
    if (h)
      Win98Region::Destroy(*this, state, h);
  }
  void Shutdown() {
    for (unsigned i = 0; i < capacity; ++i)
      if (blocks[i].range)
        Free(Raw(i));
    delete[] blocks;blocks=nullptr;capacity=0;
    state = {};
  }
} region_store;
unsigned region_serial_depth = 0;
struct RegionSerial {
  RegionSerial() {
    if (!region_serial_depth++)
      region_store.state.serial = bitmap_store.state.serial;
  }
  ~RegionSerial() {
    if (!--region_serial_depth)
      bitmap_store.state.serial = region_store.state.serial;
  }
};
const RECT &ClipAt(const State &s, unsigned i) {
  return s.clip_snapshot ? s.clip_snapshot->rectangles[i] : s.clip[i];
}
void RetainSnapshot(ClipSnapshot *p) {
  if (p)
    ++p->references;
}
void ReleaseSnapshot(ClipSnapshot *p) {
  if (p && !--p->references) {
    RegionSerial serial;
    region_store.Destroy(p->raw);
    free(p);
  }
}
void RetainClip(const State &s) {
  RetainSnapshot(s.clip_snapshot);
  RetainSnapshot(s.application_snapshot);
  RetainSnapshot(s.system_snapshot);
  RetainSnapshot(s.meta_snapshot);
}
void ReleaseClip(const State &s) {
  ReleaseSnapshot(s.clip_snapshot);
  ReleaseSnapshot(s.application_snapshot);
  ReleaseSnapshot(s.system_snapshot);
  ReleaseSnapshot(s.meta_snapshot);
}
unsigned RawRegion(unsigned native) {
  auto *o = Find(native, RegionObject);
  return o ? o->original_region : 0;
}
unsigned PublishRegion(unsigned raw) {
  if (!raw)
    return 0;
  unsigned native = Allocate(RegionObject);
  if (!native) {
    region_store.Destroy(raw);
    return 0;
  }
  Find(native, RegionObject)->original_region = raw;
  return native;
}
ClipSnapshot *Snapshot(unsigned raw) {
  unsigned p = region_store.Pointer(raw), bands = region_store.Read16(p + 18),
           count = 0, b = p + 30;
  for (unsigned i = 0; i < bands; ++i) {
    unsigned n = region_store.Read16(b);
    count += n / 2;
    b += n * 2 + 8;
  }
  if (count > (RegionStore::Limit - sizeof(ClipSnapshot)) / sizeof(RECT))
    return nullptr;
  void *allocation = malloc(sizeof(ClipSnapshot) + count * sizeof(RECT));
  if (!allocation)
    return nullptr;
  auto *s = new (allocation) ClipSnapshot{
      1, raw, count,
      reinterpret_cast<RECT *>(static_cast<unsigned char *>(allocation) +
                               sizeof(ClipSnapshot))};
  unsigned index = 0;
  b = p + 30;
  for (unsigned i = 0; i < bands; ++i) {
    unsigned n = region_store.Read16(b);
    for (unsigned j = 0; j < n; j += 2) {
      s->rectangles[index++] = {
          Win98Region::S(region_store.Read16(b + 6 + j * 2)),
          Win98Region::S(region_store.Read16(b + 2)),
          Win98Region::S(region_store.Read16(b + 8 + j * 2)),
          Win98Region::S(region_store.Read16(b + 4))};
    }
    b += n * 2 + 8;
  }
  return s;
}
// The bounds of the surface of a DC: its bitmap, or the page of a device DC.
bool SurfaceBounds(const State &s, RECT &out) {
  if (s.device_width > 0 && s.device_height > 0) {
    out = RECT{0, 0, s.device_width, s.device_height};
    return true;
  }
  const auto *bitmap = Find(s.bitmap, BitmapObject);
  if (!bitmap)
    return false;
  out = RECT{0, 0, bitmap->bitmap.width, bitmap->bitmap.height};
  return true;
}
unsigned DefaultClip(const State &s) {
  RECT bounds{};
  if (!SurfaceBounds(s, bounds))
    return 0;
  return region_store.Create(
      {0, 0, unsigned(bounds.right), unsigned(bounds.bottom)});
}
unsigned CopyClip(const State &s, bool application) {
  auto *p = application ? s.application_snapshot : s.system_snapshot;
  return p ? region_store.Clone(p->raw) : DefaultClip(s);
}
bool ReplaceClip(State &s, unsigned raw, bool application) {
  ClipSnapshot *candidate = nullptr;
  if (raw) {
    candidate = Snapshot(raw);
    if (!candidate) {
      region_store.Destroy(raw);
      return false;
    }
  }
  auto *app = application ? candidate : s.application_snapshot;
  auto *system = application ? s.system_snapshot : candidate;
  auto *meta = s.meta_snapshot;
  ClipSnapshot *effective = nullptr;
  if (app || system || meta) {
    // Raster providers intersect this application/system result with the
    // current bitmap. Retaining old bitmap bounds here would clip later
    // SelectObject/resize operations to the previous surface dimensions.
    // intersects the meta region as well.
    unsigned result = region_store.Clone((system ? system : app ? app : meta)->raw);
    if (!result) {
      ReleaseSnapshot(candidate);
      return false;
    }
    bool okay = true;
    if (system && app)
      okay = Win98Region::Combine(region_store, region_store.state, result,
                                  result, app->raw, 1) != 0;
    if (okay && meta && (system || app))
      okay = Win98Region::Combine(region_store, region_store.state, result,
                                  result, meta->raw, 1) != 0;
    if (okay)
      effective = Snapshot(result);
    if (!effective) {
      region_store.Destroy(result);
      ReleaseSnapshot(candidate);
      return false;
    }
  }
  ReleaseSnapshot(s.clip_snapshot);
  s.clip_snapshot = effective;
  auto *&target = application ? s.application_snapshot : s.system_snapshot;
  ReleaseSnapshot(target);
  target = candidate;
  s.clips = effective ? effective->count : 1;
  s.clip[0] = {-32767, -32767, 32767, 32767};
  return true;
}
// Native ordinary-DC visible realization. The original result uses
// effective(+24), after intersection with visible(+CA). The cached snapshots
// omit bitmap bounds so that SelectObject/resize never preserves stale limits.
unsigned EffectiveClipBox(const State &s, RECT &out) {
  out = RECT{};
  RECT bounds{};
  if (!SurfaceBounds(s, bounds))
    return 0;
  RECT box{};
  unsigned long long area = 0;
  bool found = false;
  for (unsigned i = 0; i < s.clips; ++i) {
    const RECT &r = ClipAt(s, i);
    RECT clipped{r.left > 0 ? r.left : 0, r.top > 0 ? r.top : 0,
                 r.right < bounds.right ? r.right : bounds.right,
                 r.bottom < bounds.bottom ? r.bottom : bounds.bottom};
    if (clipped.left >= clipped.right || clipped.top >= clipped.bottom)
      continue;
    area += (unsigned long long)(clipped.right - clipped.left) *
            unsigned(clipped.bottom - clipped.top);
    if (!found) {
      box = clipped;
      found = true;
    } else {
      if (clipped.left < box.left)
        box.left = clipped.left;
      if (clipped.top < box.top)
        box.top = clipped.top;
      if (clipped.right > box.right)
        box.right = clipped.right;
      if (clipped.bottom > box.bottom)
        box.bottom = clipped.bottom;
    }
  }
  if (!found)
    return 1;
  out = box;
  // Snapshot rectangles are disjoint; equal areas prove a filled rectangle.
  return area == (unsigned long long)(box.right - box.left) *
                     unsigned(box.bottom - box.top)
             ? 2
             : 3;
}
unsigned EffectiveClipType(const State &s) {
  RECT box{};
  return EffectiveClipBox(s, box);
}
// SetMetaRgn, GDI: the application clip becomes the meta region, or is
// intersected into it, and is gone afterwards; then Windows 98 builds the
// effective clip again. Answers the complexity of the effective clip, zero
// on failure.
unsigned SetMetaRegion(State &s) {
  if (s.application_snapshot) {
    unsigned meta = 0;
    if (!s.meta_snapshot)
      meta = region_store.Clone(s.application_snapshot->raw);
    else {
      meta = region_store.Clone(s.meta_snapshot->raw);
      if (meta && !Win98Region::Combine(region_store, region_store.state, meta, meta,
                                        s.application_snapshot->raw, 1)) {
        region_store.Destroy(meta);
        return 0;
      }
    }
    if (!meta)
      return 0;
    ClipSnapshot *snapshot = Snapshot(meta);
    if (!snapshot) {
      region_store.Destroy(meta);
      return 0;
    }
    ReleaseSnapshot(s.meta_snapshot);
    s.meta_snapshot = snapshot;
    if (!ReplaceClip(s, 0, true))
      return 0;
  }
  return EffectiveClipType(s);
}

