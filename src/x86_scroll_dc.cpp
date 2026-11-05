// SPDX-License-Identifier: GPL-2.0-or-later
// USER32!ScrollDC, the device-context half of the scroll worker that
// ScrollWindowEx shares.
#include "x86_app.h"
#include "win98_rect.h"

namespace X86 {
namespace {
using Rect = Win98::Rect;
// ScrollDC marshals integer arguments and RECT fields to Win16 widths before
// calling the shared scrolling operation.
int Word(unsigned value) { return int(short(value & 0xffffu)); }
void Narrow(Rect &r)
{
    r = {Word(unsigned(r.left)), Word(unsigned(r.top)), Word(unsigned(r.right)), Word(unsigned(r.bottom))};
}
bool Empty(const Rect &r) { return r.right <= r.left || r.bottom <= r.top; }
Rect Moved(const Rect &r, int dx, int dy)
{
    return {r.left + dx, r.top + dy, r.right + dx, r.bottom + dy};
}
// GDI.172 on a region the caller owns: the port expresses SetRectRgn as a copy
// of a fresh rectangle region, which is what the original worker's scratch
// regions amount to.
bool SetRegionRect(unsigned region, const Rect &r)
{
    const unsigned rectangle = Gdi::CreateRegionRect(r.left, r.top, r.right, r.bottom);
    if (!rectangle) return false;
    const bool okay = Gdi::CombineRegions(region, rectangle, 0, 5) != 0;
    Gdi::DeleteObject(rectangle);
    return okay;
}
} // namespace

unsigned Application::ScrollDcApi(ApiName, const unsigned *a)
{
    // USER validates the arguments, then calls the worker
    // with the device-context flag set and no ScrollWindowEx flags.
    const unsigned type = Gdi::GetObjectType(a[0]);
    if (type != 3 && type != 4 && type != 10 && type != 12)
    {
        // GDI.462 has to answer one of the DC kinds 7 to 0Bh of the 16-bit
        // numbering: DC, information DC, disabled DC, metafile DC, metafile.
        last_error = 6;
        return 0;
    }
    Rect scroll = {}, limit = {};
    // and probe eight bytes at each rectangle and accept a null
    // pointer; the 32-bit wrapper has already read sixteen.
    if (a[3] && !Read(a[3], &scroll, 16)) return 0;
    if (a[4] && !Read(a[4], &limit, 16)) return 0;
    // a non-null update region has to be a region (16-bit type 6).
    if (a[5] && Gdi::GetObjectType(a[5]) != 8) { last_error = 6; return 0; }
    // probes the update rectangle for writing.
    if (a[6] && !virtual_memory.Access(a[6], 16, true)) { last_error = 6; return 0; }
    Narrow(scroll);
    Narrow(limit);
    const int logical_dx = Word(a[1]), logical_dy = Word(a[2]);

    // 1a3c: the clip box is the fallback scroll rectangle and the bound every
    // rectangle of the simple path is cut to.
    Rect box = {};
    unsigned kind = Gdi::GetClipBox(a[0], &box);
    Rect source = a[3] ? scroll : box;
    // 1a93: on a device context the worker moves into device units, including
    // the scroll vector, which it converts as the rectangle {0,0,dx,dy}.
    Gdi::DeviceRect(a[0], box);
    Gdi::DeviceRect(a[0], source);
    if (a[4]) Gdi::DeviceRect(a[0], limit);
    Rect vector = {0, 0, logical_dx, logical_dy};
    Gdi::DeviceRect(a[0], vector);
    const int dx = vector.right - vector.left, dy = vector.bottom - vector.top;

    // 0x2010: nothing is scrolled, but the outputs are still cleared and the
    // answer is TRUE. No coordinate goes back through DPtoLP on this path.
    auto nothing = [&]() -> unsigned {
        const Rect empty = {0, 0, 0, 0};
        if (a[5] && !SetRegionRect(a[5], empty)) return 0;
        if (a[6] && !Store(a[6], &empty, 16)) return 0;
        return 1;
    };
    if (!kind) return 0;                                   // 1af8, no clip box
    if (kind == 1) return nothing();                       // NULLREGION
    unsigned clip_region = 0;
    if (kind == 3 && !(clip_region = Gdi::ClipRegionObject(a[0]))) return 0;   // 1b0a
    struct Owned {
        unsigned handles[8] = {};
        unsigned count = 0;
        unsigned Keep(unsigned h) { if (h && count < 8) handles[count++] = h; return h; }
        ~Owned() { for (unsigned i = 0; i < count; ++i) Gdi::DeleteObject(handles[i]); }
    } owned;
    owned.Keep(clip_region);

    Rect destination = Moved(source, dx, dy);              // 1b17
    if (a[4])
    {
        if (kind == 2)
        {
            // 1b44: a simple clip box is cut by the clip rectangle directly.
            if (!Win98::IntersectRect(&box, &box, &limit)) return nothing();
        }
        else
        {
            // 1b73: a complex region is intersected with the clip rectangle,
            // and its new complexity decides the same three ways.
            const unsigned rectangle = owned.Keep(
                Gdi::CreateRegionRect(limit.left, limit.top, limit.right, limit.bottom));
            if (!rectangle) return 0;
            kind = unsigned(Gdi::CombineRegions(clip_region, rectangle, clip_region, 1));
            if (!kind) return 0;
            if (kind == 1) return nothing();
            if (kind == 2 && !Gdi::RegionBounds(clip_region, &box)) return 0;   // 1bac
        }
    }

    unsigned result = kind;
    Rect blit = {}, updated = {};
    bool have_update = false;
    unsigned update_region = a[5];
    if (kind == 2)
    {
        // 1bd0: one rectangle clips everything, so the update area is rectangle
        // arithmetic until it is not, and then the region branch takes
        // over.
        const Rect original = source;
        Win98::IntersectRect(&destination, &destination, &box);
        const bool visible = Win98::IntersectRect(&source, &source, &box) != 0;
        blit = Moved(source, dx, dy);
        Win98::IntersectRect(&blit, &blit, &destination);
        bool by_region = false;
        if (!visible)
            updated = destination;                          // 1c3d
        else
        {
            Rect overlap;
            if (Win98::IntersectRect(&overlap, &source, &destination))
            {
                // 1c5f: a diagonal scroll leaves an L, which no rectangle holds.
                if (dx && dy) by_region = true;
                else
                {
                    // 1c6d and 1c81. USER and are the same rule:
                    // subtract only where the intersection covers one axis of
                    // the rectangle completely.
                    Win98::SubtractRect(&updated, &source, &destination);
                    Win98::SubtractRect(&updated, &updated, &blit);
                }
            }
            else if (Win98::EqualRect(&source, &original))
                updated = source;                           // 1c94, 1ca7
            else
                by_region = true;
        }
        if (!by_region)
        {
            have_update = true;
            if (a[5] && !SetRegionRect(a[5], updated)) return 0;     // 1cca
            result = Empty(updated) ? 1 : 2;                         // 1ce4
        }
        else
        {
            // 1d03: the caller's region, or a scratch region when only the
            // rectangle was asked for. Without either output there is nothing
            // to compute and the clip complexity stays the answer.
            if (!update_region && a[6]) update_region = owned.Keep(Gdi::CreateRegionRect(0, 0, 0, 0));
            if (update_region)
            {
                const unsigned rectangle = owned.Keep(
                    Gdi::CreateRegionRect(source.left, source.top, source.right, source.bottom));
                if (!rectangle || !SetRegionRect(update_region, destination)) return 0;
                if (!Gdi::CombineRegions(update_region, update_region, rectangle, 2)) return 0;
                if (!SetRegionRect(rectangle, blit)) return 0;
                result = unsigned(Gdi::CombineRegions(update_region, update_region, rectangle, 4));
                if (!result) return 0;
                if (a[6]) have_update = Gdi::RegionBounds(update_region, &updated);
            }
        }
    }
    else
    {
        // 1deb: a complex clip region turns every rectangle into a region. The
        // original offsets the blit region by the DC origin for SelectVisRgn;
        // this port hands the clip list to GDI in logical units instead, and
        // GDI applies the origin itself.
        const unsigned source_region = owned.Keep(
            Gdi::CreateRegionRect(source.left, source.top, source.right, source.bottom));
        const unsigned destination_region = owned.Keep(
            Gdi::CreateRegionRect(destination.left, destination.top, destination.right, destination.bottom));
        const unsigned moved = owned.Keep(Gdi::CreateRegionRect(0, 0, 0, 0));
        if (!source_region || !destination_region || !moved) return 0;
        if (!Gdi::CombineRegions(source_region, source_region, clip_region, 1)) return 0;      // 1e20
        if (!Gdi::CombineRegions(destination_region, destination_region, clip_region, 1)) return 0;  // 1e4c
        if (!Gdi::CombineRegions(moved, source_region, 0, 5)) return 0;                        // 1e73
        Gdi::OffsetRegionObject(moved, dx, dy);                                                // 1e8a
        const unsigned covered = unsigned(Gdi::CombineRegions(moved, moved, destination_region, 1));
        if (!covered) return 0;                                                                // 1f20
        if (!update_region && a[6]) update_region = owned.Keep(Gdi::CreateRegionRect(0, 0, 0, 0));
        if (update_region)
        {
            // 1f3d: everything either rectangle covers, minus what the copy
            // paints, is what the caller still owes.
            result = unsigned(Gdi::CombineRegions(update_region, destination_region, source_region, 2));
            if (!result) return 0;
            if (covered != 1)
                result = unsigned(Gdi::CombineRegions(update_region, update_region, moved, 4));   // 1f63
            if (a[6]) have_update = Gdi::RegionBounds(update_region, &updated);                   // 1f79
        }
        if (covered != 1 && !ScrollRegionCopy(a[0], destination, moved, logical_dx, logical_dy))
            return 0;
        if (a[6] && have_update)
        {
            Gdi::LogicalRect(a[0], updated);
            if (!Store(a[6], &updated, 16)) return 0;
        }
        // Convert region complexity to BOOL: zero remains zero; nonzero
        // becomes one.
        return result != 0;
    }

    // 1d8e: the copy runs only over a rectangle with area, and BitBlt works in
    // logical units, so the device rectangle goes back through DPtoLP first
    // while the scroll vector stays the one the caller passed.
    if (!Empty(blit))
    {
        Rect target = blit;
        Gdi::LogicalRect(a[0], target);
        Gdi::BitBlt(a[0], target.left, target.top, target.right - target.left,
                    target.bottom - target.top, a[0], target.left - logical_dx,
                    target.top - logical_dy, 0x00cc0020);
    }
    // 1fef: the update rectangle leaves in logical units.
    if (a[6] && have_update)
    {
        Gdi::LogicalRect(a[0], updated);
        if (!Store(a[6], &updated, 16)) return 0;
    }
    return result != 0;
}

// 1f98: SaveVisRgn, SelectVisRgn with the region that the copy may touch, the
// copy itself, RestoreVisRgn. This port has no separate visible region object,
// so the same effect comes from saving the device context, intersecting its
// system clip list with the region's rectangles and restoring afterwards.
bool Application::ScrollRegionCopy(unsigned dc, const Gdi::RECT &destination, unsigned region,
                                  int logical_dx, int logical_dy)
{
    const unsigned count = Gdi::RegionRectangles(region, nullptr, 0, 0, 0);
    if (!count) return true;
    auto *bands = new Gdi::RECT[count];
    bool okay = Gdi::RegionRectangles(region, bands, count, 0, 0) == count;
    for (unsigned i = 0; okay && i < count; ++i) okay = Gdi::LogicalRect(dc, bands[i]);
    Gdi::RECT target = destination;
    const int saved = okay ? Gdi::SaveDC(dc) : 0;
    if (saved)
    {
        if (Gdi::IntersectClipRegion(dc, bands, count) && Gdi::LogicalRect(dc, target))
            Gdi::BitBlt(dc, target.left, target.top, target.right - target.left,
                        target.bottom - target.top, dc, target.left - logical_dx,
                        target.top - logical_dy, 0x00cc0020);
        else
            okay = false;
        Gdi::RestoreDC(dc, saved);
    }
    else
        okay = false;
    delete[] bands;
    return okay;
}
} // namespace X86
