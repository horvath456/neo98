// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Native implementation of the leaf routines in the imported Win98 USER32.
// Windows LONG/BOOL remain 32-bit on our ARM64 target.
namespace Win98 {
struct Rect { int left, top, right, bottom; };
static_assert(sizeof(int) == 4 && sizeof(Rect) == 16, "Windows RECT layout");
int SetRect(Rect *, int, int, int, int);
int SetRectEmpty(Rect *);
int CopyRect(Rect *, const Rect *);
int IsRectEmpty(const Rect *);
int PtInRect(const Rect *, int, int);
int OffsetRect(Rect *, int, int);
int InflateRect(Rect *, int, int);
int IntersectRect(Rect *, const Rect *, const Rect *);
int EqualRect(const Rect *, const Rect *);
int SubtractRect(Rect *, const Rect *, const Rect *);
int UnionRect(Rect *, const Rect *, const Rect *);
// Returns zero on success, or the failing vector's one-based index.
unsigned OriginalSelfTest();
unsigned OriginalTestCount();
}
