// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NEO98_WIN98_REGION_H
#define NEO98_WIN98_REGION_H
// Native Win98 GDI.EXE region storage and scan-band algorithms.
// Memory, handle mapping and heap calls are explicit native providers.
namespace Win98Region {
using U = unsigned;
static_assert(sizeof(U) == 4,
              "Original region arithmetic requires 32-bit unsigned");
inline U W(U x) { return x & 65535u; }
inline int S(U x) {
  return (x & 32768u) ? int(x & 65535u) - 65536 : int(x & 65535u);
}
struct Rect {
  U left, top, right, bottom;
};
struct State {
  U serial, cached_count, cached[8], scratch_handle, scratch_bytes;
};
template <class E> void Zero(E &e, U p, U n) {
  for (U i = 0; i < n; ++i)
    e.Write8(p + i, 0);
}
template <class E> Rect Bounds(E &e, U h) {
  U p = e.Pointer(h);
  return {e.Read16(p + 22), e.Read16(p + 24), e.Read16(p + 26),
          e.Read16(p + 28)};
}
template <class E> U Complexity(E &e, U h) {
  U p = e.Pointer(h);
  return int(e.Read32(p + 14)) > 42 ? 3 : e.Read16(p + 18) + 1;
}
// SetRect preserves the original signature and owner fields.
template <class E> U Set(E &e, State &s, U h, const Rect &r) {
  U signature = 0x4f4c, owner = 0, bytes = S(r.left) < S(r.right) ? 42 : 30;
  if (h) {
    U p = e.Pointer(h);
    signature = e.Read16(p + 2);
    owner = e.Read16(p + 12);
  } else if (s.cached_count)
    h = s.cached[--s.cached_count];
  else if (!(h = e.Allocate(42)))
    return 0;
  U p = e.Pointer(h);
  e.Write16(p, 0);
  e.Write16(p + 2, signature);
  e.Write32(p + 4, s.serial++);
  e.Write32(p + 8, 0);
  e.Write16(p + 12, owner);
  e.Write32(p + 14, bytes);
  if (bytes == 30)
    Zero(e, p + 18, 24);
  else {
    e.Write16(p + 18, 1);
    e.Write16(p + 20, 2);
    e.Write16(p + 22, r.left);
    e.Write16(p + 24, r.top);
    e.Write16(p + 26, r.right);
    e.Write16(p + 28, r.bottom);
    e.Write16(p + 30, 2);
    e.Write16(p + 32, r.top);
    e.Write16(p + 34, r.bottom);
    e.Write16(p + 36, r.left);
    e.Write16(p + 38, r.right);
    e.Write16(p + 40, 2);
  }
  return h;
}
template <class E> U Create(E &e, State &s, const Rect &r) {
  return Set(e, s, 0, r);
}
// entered after generic DeleteObject validation.
template <class E> U Destroy(E &e, State &s, U h) {
  U p = e.Pointer(h);
  if (S(s.cached_count) < 8 && int(e.Read32(p + 14)) <= 42) {
    e.Write16(p + 2, 0);
    s.cached[s.cached_count++] = h;
    return 1;
  }
  return e.Free(h) == 0 ? 1 : 0;
}
// Preserve the original pending-band bytes after coalescing.
template <class E>
U FinishBand(E &e, U base, U &previous, U &current, U edges) {
  e.Write16(current, edges);
  if (edges > e.Read16(base + 20))
    e.Write16(base + 20, edges);
  bool merge = false;
  if (previous != current && edges == e.Read16(previous) &&
      e.Read16(current + 2) == e.Read16(previous + 4)) {
    merge = true;
    for (U i = 0; i < edges; ++i)
      if (e.Read16(previous + 6 + i * 2) != e.Read16(current + 6 + i * 2)) {
        merge = false;
        break;
      }
  }
  if (merge) {
    e.Write16(previous + 4, e.Read16(current + 4));
    e.Write16(base + 18, W(e.Read16(base + 18) - 1));
  } else {
    if (previous == current) {
      e.Write16(base + 24, e.Read16(previous + 2));
      e.Write16(base + 22, 32767);
      e.Write16(base + 26, 32769);
      e.Write16(base + 28, 32769);
    }
    previous = current;
    current += 8 + W(edges * 2);
    U right = e.Read16(current - 4), left = e.Read16(previous + 6);
    if (S(right) > S(e.Read16(base + 26)))
      e.Write16(base + 26, right);
    if (S(left) < S(e.Read16(base + 22)))
      e.Write16(base + 22, left);
  }
  U bottom = e.Read16(previous + 4);
  if (S(bottom) > S(e.Read16(base + 28)))
    e.Write16(base + 28, bottom);
  e.Write16(base + 18, W(e.Read16(base + 18) + 1));
  return bottom;
}
// Original truth table; each XOR records before/after membership.
constexpr unsigned char edge_table[64] = {
    0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0};
// signed merge of horizontal edge lists.
template <class E> U MergeEdges(E &e, U a, U na, U b, U nb, U output, U mode) {
  U i = 0, j = 0, count = 0, status = mode & 48;
  while (i < na || j < nb) {
    status &= ~3u;
    U x;
    if (i < na &&
        (j == nb || S(e.Read16(a + i * 2)) < S(e.Read16(b + j * 2)))) {
      x = e.Read16(a + i++ * 2);
      status ^= 5;
    } else if (j < nb &&
               (i == na || S(e.Read16(b + j * 2)) < S(e.Read16(a + i * 2)))) {
      x = e.Read16(b + j++ * 2);
      status ^= 10;
    } else {
      x = e.Read16(a + i++ * 2);
      ++j;
      status ^= 15;
    }
    if (edge_table[status]) {
      e.Write16(output + count * 2, x);
      ++count;
    }
  }
  e.Write16(output + count * 2, count);
  return count;
}
// Sweep Y boundaries and resize the temporary result as required.
template <class E> U Sweep(E &e, U output, U a, U b, U mode, U capacity) {
  U pa = e.Pointer(a), pb = e.Pointer(b), base = e.Pointer(output),
    ypa = pa + 32, ypb = pb + 32, xa = pa + 36, xb = pb + 36;
  int na = S(W(e.Read16(pa + 18) * 2)), nb = S(W(e.Read16(pb + 18) * 2));
  U previous = base + 30, current = previous, changes = 0,
    control = ((mode & 48) >> 1) | 6;
  Zero(e, base + 14, 16);
  static constexpr unsigned char actions[16] = {3, 0, 0, 0, 3, 1, 2, 0,
                                                3, 1, 2, 0, 3, 1, 0, 0};
  for (;;) {
    control ^= changes;
    U closed = changes & control;
    if (closed & 4) {
      ypa += (e.Read16(ypa - 6) + 1) * 2;
      xa = ypa + 6;
      ypa += 2;
    }
    if (closed & 2) {
      ypb += (e.Read16(ypb - 6) + 1) * 2;
      xb = ypb + 6;
      ypb += 2;
    }
    U y;
    changes = 0;
    --na;
    if (na >= 0) {
      y = e.Read16(ypa);
      ypa += 2;
      --nb;
      if (nb < 0 || S(y) < S(e.Read16(ypb))) {
        ++nb;
        changes = 4;
      } else if (S(y) == S(e.Read16(ypb))) {
        ypb += 2;
        changes = 6;
      } else {
        ypa -= 2;
        ++na;
        y = e.Read16(ypb);
        ypb += 2;
        changes = 2;
      }
    } else {
      --nb;
      if (nb < 0) {
        e.Write32(base + 14, current - base);
        return current - base;
      }
      ++na;
      y = e.Read16(ypb);
      ypb += 2;
      changes = 2;
    }
    U action = actions[control >> 1];
    if (!action) {
      e.Write16(current + 2, y);
      continue;
    }
    e.Write16(current + 4, y);
    if (e.Read16(current + 2) == y)
      continue;
    U ca = action == 2 ? 0 : e.Read16(xa - 6),
      cb = action == 1 ? 0 : e.Read16(xb - 6),
      estimate = action == 1   ? ca
                 : action == 2 ? cb
                               : W(ca + cb);
    U need = current + estimate * 2 + 14, limit = base + capacity;
    if (need > limit) {
      U extra = need - limit;
      if (extra < 256)
        extra += 256;
      capacity += extra;
      U oldbase = base;
      U moved = e.Reallocate(output, capacity);
      if (!moved)
        return 0;
      base = e.Pointer(moved);
      previous = base + (previous - oldbase);
      current = base + (current - oldbase);
    }
    U edges;
    if (action == 3)
      edges = MergeEdges(e, xa, ca, xb, cb, current + 6, mode);
    else {
      edges = action == 1 ? ca : cb;
      e.Move(current + 6, action == 1 ? xa : xb, edges * 2);
      e.Write16(current + 6 + edges * 2, edges);
    }
    if (edges)
      y = FinishBand(e, base, previous, current, edges);
    else
      y = e.Read16(current + 4);
    e.Write16(current + 2, y);
  }
}
// CopyRgn retains destination owner, signature and reference
// fields.
template <class E> U Copy(E &e, U destination, U source) {
  U src = e.Pointer(source), dst = e.Pointer(destination),
    owner = e.Read16(dst + 12), signature = e.Read16(dst + 2);
  U n = e.Read32(src + 14);
  if (n != e.Read32(dst + 14) && e.Size(destination) - n >= 84) {
    U h = e.Reallocate(destination, n < 42 ? 42 : n);
    if (!h)
      return 0;
    dst = e.Pointer(h);
    src = e.Pointer(source);
  }
  U refs = e.Read16(dst + 8);
  e.Move(dst, src, n);
  e.Write16(dst + 8, refs);
  e.Write16(dst + 12, owner);
  e.Write16(dst + 2, signature);
  return signature;
}
template <class E> U IntersectRectangles(E &e, U dst, U a, U b) {
  Rect x = Bounds(e, a), y = Bounds(e, b);
  Rect r = {S(x.left) > S(y.left) ? x.left : y.left,
            S(x.top) > S(y.top) ? x.top : y.top,
            S(x.right) < S(y.right) ? x.right : y.right,
            S(x.bottom) < S(y.bottom) ? x.bottom : y.bottom};
  U p = e.Pointer(dst);
  e.Write32(p + 14, 42);
  if (S(r.left) < S(r.right) && S(r.top) < S(r.bottom)) {
    e.Write16(p + 18, 1);
    e.Write16(p + 20, 2);
    e.Write16(p + 22, r.left);
    e.Write16(p + 24, r.top);
    e.Write16(p + 26, r.right);
    e.Write16(p + 28, r.bottom);
    e.Write16(p + 30, 2);
    e.Write16(p + 32, r.top);
    e.Write16(p + 34, r.bottom);
    e.Write16(p + 36, r.left);
    e.Write16(p + 38, r.right);
    e.Write16(p + 40, 2);
    return 2;
  }
  Zero(e, p + 18, 24);
  return 1;
}
// internal region clone used by clipping and DC state.
template <class E> U Clone(E &e, State &s, U source) {
  U src = e.Pointer(source), h = 0;
  if (int(e.Read32(src + 14)) <= 42 && s.cached_count)
    h = s.cached[--s.cached_count];
  else
    h = e.Allocate(e.Size(source));
  if (!h)
    return 0;
  U p = e.Pointer(h);
  e.Write16(p + 2, 0x6f4c);
  e.Write16(p + 12, e.Read16(src + 12));
  e.Write16(p + 8, 0);
  // The selected copy cannot resize: the destination has source capacity.
  Copy(e, h, source);
  return h;
}
// Complete boolean-object path, including rectangle AND shortcut.
template <class E> U Combine(E &e, State &s, U dst, U a, U b, U mode) {
  U result = 0;
  if (S(mode) >= 5) {
    if (Copy(e, dst, a))
      result = Complexity(e, dst);
  } else if (b) {
    U pa = e.Pointer(a), pb = e.Pointer(b);
    if (mode == 1 && e.Read32(pa + 14) == 42 && e.Read32(pb + 14) == 42) {
      return IntersectRectangles(e, dst, a, b);
    }
    U pd = e.Pointer(dst), owner = e.Read16(pd + 12),
      signature = e.Read16(pd + 2);
    U n = Sweep(e, s.scratch_handle, a, b, W((mode - 1) * 16), 256);
    if (n) {
      if (int(n) < 42)
        n = 42;
      U temp = e.Pointer(s.scratch_handle);
      pd = e.Pointer(dst);
      e.Write16(temp, e.Read16(pd));
      e.Write16(temp + 2, signature);
      e.Write16(temp + 8, 0);
      e.Write32(temp + 14, n);
      bool ok = true;
      if (e.Size(dst) - n >= 84) {
        U h = e.Reallocate(dst, n);
        if (!h)
          ok = false;
        else
          pd = e.Pointer(h);
      }
      if (ok) {
        U refs = e.Read16(pd + 8);
        e.Move(pd, temp, e.Read32(temp + 14));
        e.Write16(pd + 8, refs);
        e.Write32(pd + 4, s.serial++);
        e.Write16(pd + 12, owner);
        result = Complexity(e, dst);
      }
    }
  }
  if (e.Size(s.scratch_handle) != s.scratch_bytes)
    e.Reallocate(s.scratch_handle, 256);
  return result;
}
// WORD additions intentionally wrap and do not change serial.
template <class E> U Offset(E &e, U h, U x, U y) {
  U p = e.Pointer(h), bands = e.Read16(p + 18);
  if (!bands)
    return Complexity(e, h);
  e.Write16(p + 22, W(e.Read16(p + 22) + x));
  e.Write16(p + 26, W(e.Read16(p + 26) + x));
  e.Write16(p + 24, W(e.Read16(p + 24) + y));
  e.Write16(p + 28, W(e.Read16(p + 28) + y));
  U b = p + 30;
  for (U i = 0; i < bands; ++i) {
    e.Write16(b + 2, W(e.Read16(b + 2) + y));
    e.Write16(b + 4, W(e.Read16(b + 4) + y));
    U n = e.Read16(b);
    for (U j = 0; j < n; ++j)
      e.Write16(b + 6 + j * 2, W(e.Read16(b + 6 + j * 2) + x));
    b += n * 2 + 8;
  }
  return Complexity(e, h);
}
// Equality includes all used bytes after the common object
// prefix.
template <class E> U Equal(E &e, U a, U b) {
  U pa = e.Pointer(a), pb = e.Pointer(b), n = e.Read32(pa + 14);
  if (n != e.Read32(pb + 14))
    return 0;
  for (U i = 18; i < n; ++i)
    if (e.Read8(pa + i) != e.Read8(pb + i))
      return 0;
  return 1;
}
// The success result preserves the high byte of the right edge.
template <class E> U Point(E &e, U h, U x, U y) {
  U p = e.Pointer(h), n = e.Read16(p + 18), b = p + 30;
  for (U i = 0; i < n; ++i) {
    U count = e.Read16(b);
    if (S(y) < S(e.Read16(b + 2)))
      return 0;
    if (S(y) < S(e.Read16(b + 4)))
      for (U j = 0; j < count; j += 2) {
        if (S(x) < S(e.Read16(b + 6 + j * 2)))
          return 0;
        U right = e.Read16(b + 8 + j * 2);
        if (S(x) < S(right))
          return (right & 0xff00) | 1;
      }
    b += count * 2 + 8;
  }
  return 0;
}
// Returns required size without modifying a short destination.
template <class E> U Data(E &e, U h, U capacity, U out) {
  U p = e.Pointer(h), bands = e.Read16(p + 18), required = 32, b = p + 30;
  for (U i = 0; i < bands; ++i) {
    U n = e.Read16(b);
    required += (n & 65534) * 8;
    b += n * 2 + 8;
  }
  if (!out || capacity < required)
    return required;
  e.Write32(out, 32);
  e.Write32(out + 4, 1);
  e.Write32(out + 8, (required - 32) / 16);
  e.Write32(out + 12, required - 32);
  Rect r = Bounds(e, h);
  e.Write32(out + 16, U(S(r.left)));
  e.Write32(out + 20, U(S(r.top)));
  e.Write32(out + 24, U(S(r.right)));
  e.Write32(out + 28, U(S(r.bottom)));
  U q = out + 32;
  b = p + 30;
  for (U i = 0; i < bands; ++i) {
    U n = e.Read16(b);
    for (U j = 0; j < n; j += 2) {
      e.Write32(q, U(S(e.Read16(b + 6 + j * 2))));
      e.Write32(q + 4, U(S(e.Read16(b + 2))));
      e.Write32(q + 8, U(S(e.Read16(b + 8 + j * 2))));
      e.Write32(q + 12, U(S(e.Read16(b + 4))));
      q += 16;
    }
    b += n * 2 + 8;
  }
  return required;
}
// Stateful rectangle enumeration, including clipping and
// direction.
struct Iterator {
  U region, flags;
  Rect clip, result;
  U band, edge, terminal;
};
template <class E> U Next(E &e, Iterator &i) {
  U flags = i.flags;
  bool initialize = (flags & 128) != 0;
  if (initialize) {
    flags &= ~128u;
    i.flags = flags;
    U p = e.Pointer(i.region);
    if (!e.Read16(p + 18))
      return 0;
    if (e.Read32(p + 14) <= 42) {
      flags |= 64;
      i.flags = flags;
      Rect r = Bounds(e, i.region);
      if (flags & 0xff00) {
        if (S(r.left) <= S(i.clip.left))
          r.left = i.clip.left;
        if (S(r.top) <= S(i.clip.top))
          r.top = i.clip.top;
        if (S(r.right) >= S(i.clip.right))
          r.right = i.clip.right;
        if (S(r.bottom) >= S(i.clip.bottom))
          r.bottom = i.clip.bottom;
        if (S(r.left) >= S(r.right) || S(r.top) >= S(r.bottom))
          return 0;
        i.result = r;
        return r.left | 1;
      }
      i.result = r;
      return flags | 1;
    }
    U first = p + 30, end = p + e.Read32(p + 14);
    if (flags & 2) {
      i.terminal = end;
      i.band = first;
    } else {
      i.terminal = first;
      i.band = end - e.Read16(end - 2) * 2 - 8;
    }
  }
  bool advance_x = !initialize, advance_y = false;
  for (;;) {
    if (advance_y) {
      if (flags & 2) {
        i.band += e.Read16(i.band) * 2 + 8;
        if (i.band == i.terminal)
          return 0;
        if ((flags & 0xff00) && S(i.clip.bottom) <= S(e.Read16(i.band + 2)))
          return 0;
      } else {
        if (i.band == i.terminal)
          return 0;
        i.band -= e.Read16(i.band - 2) * 2 + 8;
        if ((flags & 0xff00) && S(i.clip.top) >= S(e.Read16(i.band + 4)))
          return 0;
      }
      advance_x = false;
      advance_y = false;
    }
    U top = e.Read16(i.band + 2), bottom = e.Read16(i.band + 4);
    if (flags & 0xff00) {
      if (S(top) <= S(i.clip.top))
        top = i.clip.top;
      if (S(bottom) >= S(i.clip.bottom))
        bottom = i.clip.bottom;
      if (S(top) >= S(bottom)) {
        advance_y = true;
        continue;
      }
    }
    i.result.top = top;
    i.result.bottom = bottom;
    if (!advance_x)
      i.edge = i.band + 6 + ((flags & 1) ? 0 : e.Read16(i.band) * 2 - 4);
    else {
      if (e.Read16(i.band) == 2) {
        advance_y = true;
        continue;
      }
      if (flags & 1) {
        i.edge += 4;
        if (i.edge == i.band + 6 + e.Read16(i.band) * 2) {
          advance_y = true;
          continue;
        }
      } else {
        if (i.edge == i.band + 6) {
          advance_y = true;
          continue;
        }
        i.edge -= 4;
      }
    }
    U left = e.Read16(i.edge), right = e.Read16(i.edge + 2);
    if (flags & 0xff00) {
      if (S(left) <= S(i.clip.left))
        left = i.clip.left;
      if (S(right) >= S(i.clip.right))
        right = i.clip.right;
      if (S(left) >= S(right)) {
        advance_x = true;
        continue;
      }
    }
    i.result.left = left;
    i.result.right = right;
    return flags | 1;
  }
}
// region-only branch. Win3.0 callers include bottom/right edges.
template <class E>
U ContainsRect(E &e, U h, Rect r, U expected_version = 0x400) {
  if (!h)
    return 0;
  if (S(r.right) <= S(r.left)) {
    U x = r.left;
    r.left = r.right;
    r.right = x;
  }
  if (S(r.bottom) <= S(r.top)) {
    U y = r.top;
    r.top = r.bottom;
    r.bottom = y;
  }
  if (expected_version <= 0x300) {
    r.right = W(r.right + 1);
    r.bottom = W(r.bottom + 1);
  }
  Iterator i{h, 0x183, r, {}, 0, 0, 0};
  return Next(e, i);
}
// Append sorted rectangles cheaply; use the sweep for overlap.
template <class E> U Append(E &e, State &s, U h, const Rect &r) {
  if (!h)
    return 0;
  if (S(r.left) >= S(r.right) || S(r.top) >= S(r.bottom))
    return 1;
  U p = e.Pointer(h), n = e.Read32(p + 14);
  bool extend = false, add = false;
  if (e.Read16(p + 18) >= 2) {
    if (S(r.top) >= S(e.Read16(p + 28)))
      add = true;
    else if (r.bottom == e.Read16(p + 28) &&
             S(r.left) > S(e.Read16(p + n - 4)) &&
             r.top == e.Read16(p + n - e.Read16(p + n - 2) * 2 - 6))
      extend = true;
  }
  if (add || extend) {
    if (!e.Reallocate(h, n + (extend ? 4 : 12)))
      return 0;
    p = e.Pointer(h);
    U current = p + n, edges = 2;
    if (extend) {
      edges = e.Read16(current - 2) + 2;
      U old = edges - 2;
      current -= old * 2 + 8;
      e.Write16(p + 18, W(e.Read16(p + 18) - 1));
      U q = p + n - 2;
      e.Write16(q, r.left);
      e.Write16(q + 2, r.right);
      e.Write16(q + 4, edges);
    } else {
      e.Write16(current, W(p));
      e.Write16(current + 2, r.top);
      e.Write16(current + 4, r.bottom);
      e.Write16(current + 6, r.left);
      e.Write16(current + 8, r.right);
      e.Write16(current + 10, edges);
    }
    U previous = current - e.Read16(current - 2) * 2 - 8;
    FinishBand(e, p, previous, current, edges);
    e.Write32(p + 14, current - p);
    return 1;
  }
  U other = Create(e, s, r);
  if (!other)
    return 0;
  e.Write16(e.Pointer(other) + 12, e.CallerOwner());
  U result = Combine(e, s, h, h, other, 2);
  Destroy(e, s, other);
  return result;
}
// without the independent floating-point transform provider.
// Header size and nRgnSize are not consulted by this original branch.
template <class E> U Import(E &e, State &s, U input) {
  U h = Create(e, s, {0, 0, 0, 0});
  if (!h)
    return 0;
  if (e.Read32(input + 4) != 1) {
    Destroy(e, s, h);
    return 0;
  }
  U count = e.Read32(input + 8), p = input + 32;
  for (U i = 0; i < count; ++i, p += 16)
    if (!Append(e, s, h,
                {e.Read16(p), e.Read16(p + 4), e.Read16(p + 8),
                 e.Read16(p + 12)})) {
      Destroy(e, s, h);
      return 0;
    }
  return h;
}
} // namespace Win98Region
#endif
