// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// FloodFill and ExtFloodFill, matching Windows 98 behavior.
//
// GDI FLOODFILL and EXTFLOODFILL share an implementation.
// It takes a scan line stack from the global heap, 1F40h entries of
// four bytes with a header of seven WORDs, and halves the request while the
// allocation fails, down to 200h entries; below that it gives up and answers
// zero. The point goes through LPtoDP before the
// filler seeds the first run and Windows 98 walks the stack.
//
// The fill type decides the test. The worker treats any nonzero value as
// FLOODFILLSURFACE (1), otherwise FLOODFILLBORDER (0). The public entry
// rejects values above one with error E001h.
namespace Win98GdiFlood {
// The original stack holds runs, not single pixels. Each entry is the run that
// was just filled plus the direction it came from.
struct Run {
  int left, right, y, direction;
};
constexpr unsigned Entries = 0x1f40; //
constexpr unsigned Minimum = 0x200;  //

// `surface` answers Inside(x,y) for a pixel that may be painted at all (inside
// the bitmap and inside the clip), Read(x,y) for its current value and
// Write(x,y) to paint it with the brush. Seen/Mark keep traversal state apart
// from the destination bits: a border fill remains open after painting when
// the brush colour is not the border colour, and a patterned brush or ROP can
// leave the old pixel unchanged.
//
// `border` is true for FLOODFILLBORDER: the run spreads while the pixel is not
// the reference colour. FLOODFILLSURFACE spreads while it is.
template <class Surface>
bool Fill(Surface &surface, int x, int y, unsigned reference, bool surface_mode,
          bool *exhausted = nullptr) {
  if (exhausted)
    *exhausted = false;
  auto open = [&](int px, int py) {
    if (!surface.Inside(px, py) || surface.Seen(px, py))
      return false;
    const unsigned value = surface.Read(px, py);
    return surface_mode ? value == reference : value != reference;
  };
  if (!open(x, y))
    return false; // an unusable seed answers FALSE without painting
  unsigned capacity = Entries;
  Run *stack = nullptr;
  while (capacity >= Minimum) {
    stack = (Run *)calloc(capacity, sizeof(Run));
    if (stack)
      break;
    capacity /= 2;
  }
  if (!stack) {
    if (exhausted)
      *exhausted = true;
    return false;
  }
  unsigned top = 0;
  bool overflow = false;
  auto push = [&](int left, int right, int py, int direction) {
    if (top < capacity)
      stack[top++] = {left, right, py, direction};
    else
      overflow = true;
  };
  push(x, x, y, 0);
  bool painted = false;
  while (top && !overflow) {
    const Run run = stack[--top];
    int left = run.left, right = run.right;
    if (!open(left, run.y))
      continue;
    while (open(left - 1, run.y))
      --left;
    while (open(right + 1, run.y))
      ++right;
    for (int px = left; px <= right; ++px) {
      surface.Mark(px, run.y);
      surface.Write(px, run.y);
    }
    painted = true;
    // The visited map makes both neighbouring scan lines safe to scan every
    // time. This is necessary when the source run widens around a barrier on
    // the line it came from: that line can contain a second reachable run.
    for (int step = -1; step <= 1; step += 2) {
      const int ny = run.y + step;
      int px = left;
      while (px <= right) {
        while (px <= right && !open(px, ny))
          ++px;
        if (px > right)
          break;
        const int begin = px;
        while (px <= right && open(px, ny))
          ++px;
        push(begin, px - 1, ny, step);
      }
    }
  }
  free(stack);
  if (exhausted)
    *exhausted = overflow;
  return painted && !overflow;
}
} // namespace Win98GdiFlood
