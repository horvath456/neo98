// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_rect.h"
#include <new>
// Native disjoint update rectangles. Storage grows with the region; failed
// subtraction preserves it. This is not the original GDI region heap.
struct RectRegion
{
    static constexpr unsigned Capacity = 64;
    Win98::Rect small[Capacity] = {};
    Win98::Rect *rectangles = small;
    unsigned count = 0, capacity = Capacity;
    RectRegion() = default;
    ~RectRegion(){if(rectangles!=small)delete[] rectangles;}
    RectRegion(const RectRegion &other){*this=other;}
    RectRegion &operator=(const RectRegion &other)
    {
        if(this==&other)return *this;
        if(!Reserve(other.count)){count=0;Append(other.Bounds());return *this;}
        count=other.count;
        for(unsigned i=0;i<count;++i)rectangles[i]=other.rectangles[i];
        return *this;
    }
    RectRegion &operator=(RectRegion &&other)
    {
        if(this==&other)return *this;
        if(other.rectangles==other.small)return *this=static_cast<const RectRegion&>(other);
        if(rectangles!=small)delete[] rectangles;
        rectangles=other.rectangles;count=other.count;capacity=other.capacity;
        other.rectangles=other.small;other.count=0;other.capacity=Capacity;
        return *this;
    }
    bool Reserve(unsigned want)
    {
        if(want<=capacity)return true;
        unsigned grown=capacity;
        while(grown<want){if(grown>~0u/2)return false;grown*=2;}
        if(sizeof(Win98::Rect)>size_t(-1)/grown)return false;
        auto *more=new(std::nothrow) Win98::Rect[grown];
        if(!more)return false;
        for(unsigned i=0;i<count;++i)more[i]=rectangles[i];
        if(rectangles!=small)delete[] rectangles;
        rectangles=more;capacity=grown;return true;
    }
    void Clear() { count = 0; }
    bool Append(const Win98::Rect &r)
    {
        if (Win98::IsRectEmpty(&r))
            return true;
        if (!Reserve(count + 1))
            return false;
        rectangles[count++] = r;
        return true;
    }
    static bool Difference(RectRegion &out, const Win98::Rect &r, const Win98::Rect &cut)
    {
        Win98::Rect overlap;
        if (!Win98::IntersectRect(&overlap, &r, &cut))
            return out.Append(r);
        return out.Append({r.left, r.top, r.right, overlap.top}) &&
               out.Append({r.left, overlap.bottom, r.right, r.bottom}) &&
               out.Append({r.left, overlap.top, overlap.left, overlap.bottom}) &&
               out.Append({overlap.right, overlap.top, r.right, overlap.bottom});
    }
    bool Subtract(const Win98::Rect &cut)
    {
        RectRegion out;
        for (unsigned i = 0; i < count; ++i)
            if (!Difference(out, rectangles[i], cut))
                return false;
        *this = static_cast<RectRegion&&>(out);
        return true;
    }
    // Merges neighbours that together are one rectangle: the same columns and
    // touching rows, or the same rows and touching columns. GDI's band code
    // does the same when it combines regions; without it an area invalidated
    // one row of pixels at a time (a tree view that scrolls in steps of one
    // pixel) filled the 64 places and lost every further row.
    void Coalesce()
    {
        for (bool merged = true; merged;)
        {
            merged = false;
            for (unsigned i = 0; i < count && !merged; ++i)
                for (unsigned j = i + 1; j < count && !merged; ++j)
                {
                    Win98::Rect &a = rectangles[i];
                    const Win98::Rect &b = rectangles[j];
                    const bool columns = a.left == b.left && a.right == b.right &&
                                         (a.bottom == b.top || b.bottom == a.top);
                    const bool rows = a.top == b.top && a.bottom == b.bottom &&
                                      (a.right == b.left || b.right == a.left);
                    if (!columns && !rows)
                        continue;
                    Win98::UnionRect(&a, &a, &b);
                    rectangles[j] = rectangles[--count];
                    merged = true;
                }
        }
    }
    // Adds an area. Only allocation failure broadens the update to its
    // bounding rectangle; ordinary complex regions keep their holes.
    bool Add(const Win98::Rect &r)
    {
        RectRegion pieces;
        pieces.Append(r);
        for (unsigned i = 0; i < count; ++i)
            if (!pieces.Subtract(rectangles[i]))
            {
                Win98::Rect bounds = Bounds();
                Win98::UnionRect(&bounds, &bounds, &r);
                count = 0;
                return Append(bounds);
            }
        for (unsigned i = 0; i < pieces.count; ++i)
        {
            if (!Reserve(count + 1))
            {
                Win98::Rect bounds = Bounds();
                for (unsigned k = i; k < pieces.count; ++k)
                    Win98::UnionRect(&bounds, &bounds, &pieces.rectangles[k]);
                count = 0;
                return Append(bounds);
            }
            rectangles[count++] = pieces.rectangles[i];
        }
        Coalesce();
        return true;
    }
    Win98::Rect Bounds() const
    {
        Win98::Rect out = {};
        for (unsigned i = 0; i < count; ++i)
            Win98::UnionRect(&out, &out, &rectangles[i]);
        return out;
    }
};
