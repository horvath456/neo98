// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_rect.h"
#include "win98_rect_oracle.h"

namespace Win98 {
// USER32, including the original handling of empty source rectangles.
int SubtractRect(Rect *out, const Rect *first, const Rect *second)
{
    if (!out || !first || !second)
        return 0;
    Rect a = *first, b = *second, overlap;
    const bool intersects = IntersectRect(&overlap, &a, &b);
    *out = a;
    if (intersects)
    {
        unsigned sides = (overlap.left <= a.left) + (overlap.top <= a.top) + (overlap.right >= a.right) +
                         (overlap.bottom >= a.bottom);
        if (sides == 4)
        {
            *out = {};
            return 0;
        }
        if (sides == 3)
        {
            if (overlap.left > a.left)
                out->right = overlap.left;
            else if (overlap.right < a.right)
                out->left = overlap.right;
            else if (overlap.top > a.top)
                out->bottom = overlap.top;
            else if (overlap.bottom < a.bottom)
                out->top = overlap.bottom;
        }
    }
    return !IsRectEmpty(out);
}
// USER32 : if both inputs are empty, the second one is copied unchanged.
int UnionRect(Rect *out, const Rect *first, const Rect *second)
{
    if (!out || !first || !second)
        return 0;
    Rect a = *first, b = *second;
    bool empty_b = IsRectEmpty(&b);
    if (IsRectEmpty(&a))
    {
        *out = b;
        return !empty_b;
    }
    if (empty_b)
    {
        *out = a;
        return 1;
    }
    *out = {a.left < b.left ? a.left : b.left, a.top < b.top ? a.top : b.top,
            a.right > b.right ? a.right : b.right, a.bottom > b.bottom ? a.bottom : b.bottom};
    return 1;
}
static int Add(int a, int b) { return (int)((unsigned)a + (unsigned)b); }
static int Sub(int a, int b) { return (int)((unsigned)a - (unsigned)b); }

int SetRect(Rect *r, int l, int t, int right, int bottom)
{
    if (!r) return 0;
    *r = {l, t, right, bottom};
    return -1;
}
int SetRectEmpty(Rect *r) { return SetRect(r, 0, 0, 0, 0); }
int CopyRect(Rect *dst, const Rect *src)
{
    if (!dst || !src) return 0;
    dst->left = src->left;
    dst->top = src->top;
    dst->right = src->right;
    dst->bottom = src->bottom;
    return -1;
}
int IsRectEmpty(const Rect *r)
{
    return !r || r->right <= r->left || r->bottom <= r->top;
}
int PtInRect(const Rect *r, int x, int y)
{
    return r && x >= r->left && y >= r->top && x < r->right && y < r->bottom;
}
int OffsetRect(Rect *r, int x, int y)
{
    if (!r) return 0;
    r->left = Add(r->left, x); r->top = Add(r->top, y);
    r->right = Add(r->right, x); r->bottom = Add(r->bottom, y);
    return -1;
}
int InflateRect(Rect *r, int x, int y)
{
    if (!r) return 0;
    r->left = Sub(r->left, x); r->top = Sub(r->top, y);
    r->right = Add(r->right, x); r->bottom = Add(r->bottom, y);
    return -1;
}
int IntersectRect(Rect *dst, const Rect *a, const Rect *b)
{
    if (!dst || !a || !b) return 0;
    const int l = a->left > b->left ? a->left : b->left;
    const int r = a->right < b->right ? a->right : b->right;
    const int t = a->top > b->top ? a->top : b->top;
    const int bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    // The original changes AL only on success, leaving the other EAX bits.
    const int high_bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    if (l >= r || t >= bottom) { SetRectEmpty(dst); return 0; }
    *dst = {l, t, r, bottom};
    return (int)(((unsigned)high_bottom & 0xffffff00u) | 1u);
}
int EqualRect(const Rect *a, const Rect *b)
{
    return a && b && a->left == b->left && a->top == b->top &&
           a->right == b->right && a->bottom == b->bottom;
}

#ifndef NEO98_RUNTIME_CONTENT
using Vector = OriginalVector;
#include "../build/generated/win98_vectors.h"

const OriginalVector &OriginalCase(unsigned index) { return Vectors[index]; }

unsigned OriginalTestCount() { return sizeof(Vectors) / sizeof(Vectors[0]); }
unsigned OriginalSelfTest()
{
    for (unsigned i = 0; i < OriginalTestCount(); ++i) {
        const auto &v = Vectors[i];
        struct { unsigned guard1; Rect rects[3]; unsigned guard2; } memory =
            {0x12345678, {v.before[0], v.before[1], v.before[2]}, 0x87654321};
        Rect *p[3];
        for (int j = 0; j < 3; ++j)
            p[j] = v.pointers[j] < 0 ? nullptr : &memory.rects[v.pointers[j]];
        int result = 0;
        switch (v.operation) {
        case 0: result = SetRect(p[0], v.args[0], v.args[1], v.args[2], v.args[3]); break;
        case 1: result = SetRectEmpty(p[0]); break;
        case 2: result = CopyRect(p[0], p[1]); break;
        case 3: result = IsRectEmpty(p[0]); break;
        case 4: result = PtInRect(p[0], v.args[0], v.args[1]); break;
        case 5: result = OffsetRect(p[0], v.args[0], v.args[1]); break;
        case 6: result = InflateRect(p[0], v.args[0], v.args[1]); break;
        case 7: result = IntersectRect(p[0], p[1], p[2]); break;
        case 8: result = EqualRect(p[0], p[1]); break;
        }
        if ((unsigned)result != v.result || memory.guard1 != 0x12345678 ||
            memory.guard2 != 0x87654321) return i + 1;
        for (int j = 0; j < 3; ++j)
            if (!EqualRect(&memory.rects[j], &v.after[j])) return i + 1;
    }
    return 0;
}
#endif
}
