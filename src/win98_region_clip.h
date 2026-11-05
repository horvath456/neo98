// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_dc_map.h"
// GDI transforms (0,0) and the logical delta with AA4E,
// then subtracts the two device points in WORDs. AA6F applies DC shifts.
namespace Win98RegionClip {
// SAR on a sign-extended WORD uses CL modulo 32. Spell out sign fill so
// pre-C++20 negative right-shift behavior is not a native compiler dependency.
inline int ArithmeticShift(int value, unsigned count) {
  count &= 31;
  return value >= 0 ? int(unsigned(value) >> count)
                    : -1 - int(unsigned(-1 - value) >> count);
}
inline Win98DcMap::Point DrawingPoint(const Win98DcMap::State &s,
                                      Win98DcMap::Point p, unsigned words) {
  p = Win98DcMap::Transform(s, p, false, false, words);
  p.x = ArithmeticShift(p.x, unsigned(s.shifts.x));
  p.y = ArithmeticShift(p.y, unsigned(s.shifts.y));
  return p;
}
inline Win98DcMap::Point OffsetDelta(const Win98DcMap::State &s, unsigned x,
                                     unsigned y) {
  const auto a = DrawingPoint(s, {0, 0}, 4),
             b = DrawingPoint(s, {Win98DcMap::S(x), Win98DcMap::S(y)}, 2);
  return {Win98DcMap::S(unsigned(b.x - a.x)),
          Win98DcMap::S(unsigned(b.y - a.y))};
}
} // namespace Win98RegionClip
