// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef NEO98_WIN98_REGION_API_H
#define NEO98_WIN98_REGION_API_H
#include "win98_region.h"
namespace Win98RegionApi {
using Win98Region::Rect;
using Win98Region::S;
using Win98Region::State;
using Win98Region::U;
using Win98Region::W;
inline int Signed32(U x) { return x <= 0x7fffffffu ? int(x) : -1 - int(~x); }
// GDI32 sorts DWORD coordinates before the Win16 narrowing.
inline Rect Normalize(U left, U top, U right, U bottom) {
  if (Signed32(left) > Signed32(right)) {
    U v = left;
    left = right;
    right = v;
  }
  if (Signed32(top) > Signed32(bottom)) {
    U v = top;
    top = bottom;
    bottom = v;
  }
  return {W(left), W(top), W(right), W(bottom)};
}
template <class E> bool Valid(E &e, U h) {
  U p = e.Pointer(h);
  return p && (e.Read16(p + 2) & 0x5fff) == 0x4f4c;
}
template <class E> U Create(E &e, State &s, U left, U top, U right, U bottom) {
  U h = Win98Region::Create(e, s, Normalize(left, top, right, bottom));
  if (h)
    e.Write16(e.Pointer(h) + 12, e.CallerOwner());
  return h;
}
template <class E> U Indirect(E &e, State &s, U rectangle) {
  return Create(e, s, e.Read32(rectangle), e.Read32(rectangle + 4),
                e.Read32(rectangle + 8), e.Read32(rectangle + 12));
}
template <class E>
U Set(E &e, State &s, U h, U left, U top, U right, U bottom) {
  h = W(h);
  if (!Valid(e, h))
    return 0;
  return Win98Region::Set(e, s, h, Normalize(left, top, right, bottom));
}
// GDI32 always checks heap capacity, even for equal used sizes.
template <class E> U Copy(E &e, U destination, U source) {
  U src = e.Pointer(source), dst = e.Pointer(destination),
    owner = e.Read16(dst + 12), signature = e.Read16(dst + 2),
    refs = e.Read16(dst + 8), n = e.Read32(src + 14);
  if (e.Size(destination) - n >= 84) {
    U h = e.Reallocate(destination, n < 42 ? 42 : n);
    if (!h)
      return 0;
    dst = e.Pointer(h);
  }
  e.Move(dst, src, n);
  e.Write16(dst + 8, refs);
  e.Write16(dst + 12, owner);
  e.Write16(dst + 2, signature);
  return dst;
}
// GDI32; remaining modes dispatch to the Win16 object
// algorithm.
template <class E> U Combine(E &e, State &s, U dst, U a, U b, U mode) {
  if (!Valid(e, a) || !Valid(e, dst))
    return 0;
  if (mode == 5) {
    if (!Copy(e, dst, a))
      return 0;
    return Win98Region::Complexity(e, dst);
  }
  if (mode == 1) {
    if (!Valid(e, b))
      return 0;
    if (e.Read32(e.Pointer(a) + 14) <= 42 &&
        e.Read32(e.Pointer(b) + 14) <= 42) {
      U result = Win98Region::IntersectRectangles(e, dst, a, b);
      if (e.Size(dst) >= 126)
        e.Reallocate(dst, 42);
      return result;
    }
  }
  dst = W(dst);
  a = W(a);
  b = W(b);
  mode = W(mode);
  if (!Valid(e, dst) || !Valid(e, a) || (b && !Valid(e, b)) || S(mode) < 1 ||
      S(mode) > 5)
    return 0;
  return Win98Region::Combine(e, s, dst, a, b, mode);
}
// and are direct PE32 region queries.
template <class E> U Box(E &e, U h, U output) {
  if (!Valid(e, h))
    return 0;
  U p = e.Pointer(h);
  for (U i = 0; i < 4; ++i)
    e.Write32(output + i * 4, U(S(e.Read16(p + 22 + i * 2))));
  return Win98Region::Complexity(e, h);
}
template <class E> U Data(E &e, U h, U capacity, U output) {
  return Valid(e, h) ? Win98Region::Data(e, h, capacity, output) : 0;
}
template <class E> U Offset(E &e, U h, U x, U y) {
  h = W(h);
  return Valid(e, h) ? U(S(Win98Region::Offset(e, h, W(x), W(y)))) : 0;
}
template <class E> U Equal(E &e, U a, U b) {
  a = W(a);
  b = W(b);
  return Valid(e, a) && Valid(e, b) ? Win98Region::Equal(e, a, b) : 0;
}
template <class E> U Point(E &e, U h, U x, U y) {
  h = W(h);
  return Valid(e, h) ? U(S(Win98Region::Point(e, h, W(x), W(y)))) : 0;
}
template <class E> U ContainsRect(E &e, U h, const Rect &r, U version = 0x400) {
  h = W(h);
  return Valid(e, h) ? U(S(Win98Region::ContainsRect(e, h, r, version))) : 0;
}
} // namespace Win98RegionApi
#endif
