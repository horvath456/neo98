// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 RedrawWindow. USER32!RedrawWindow narrows its
// arguments to words, copies the RECT into four words and calls dispatch index
// 14h of the USER table, which is USER.EXE, ordinal 290.
// Windows 98 validates, turns the rect or region argument into one screen
// region, decides the invalidate/validate work and the immediate
// update, and Windows 98 walks the window and its children. GDI regions, the
// window rectangles, the per-window apply step and the two immediate paths
// are explicit providers.
namespace Win98RedrawWindow {
using U = unsigned;
inline U W(U x) { return x & 65535u; }
inline int S(U x) { return short(x); }

constexpr U Invalidate = 0x0001, InternalPaint = 0x0002, Erase = 0x0004, Validate = 0x0008;
constexpr U NoInternalPaint = 0x0010, NoErase = 0x0020, NoChildren = 0x0040, AllChildren = 0x0080;
constexpr U UpdateNow = 0x0100, EraseNow = 0x0200, Frame = 0x0400, NoFrame = 0x0800;
// Windows 98 sets 1000h before calling the worker, so the flag marks the public
// entry; Windows 98 sets 2000h in its working copy and 8000h when the window or an
// ancestor carries a window region. Windows 98 rejects any flag above 0FFFh.
constexpr U Prepared = 0x1000, Trim = 0x2000, Shaped = 0x8000;
constexpr U FlagError = 0xe002, RegionError = 0x6067, RegionObject = 6;
constexpr U DispatchIndex = 0x14, ArgumentBytes = 0x10;
// GDI region combine modes and the NULLREGION answer.
constexpr U And = 1, Diff = 4, Copy = 5, NullRegion = 1;
// The whole-window sentinel Windows 98 stores when no rect and no region is given.
constexpr U WholeWindow = 1;

struct Rect { int left = 0, top = 0, right = 0, bottom = 0; };
// The modeled WND fields this family reads: the parent DWORD at +8, the first
// child DWORD at +4, the next sibling DWORD at +0, the client origin WORDs at
// +18h/+1Ah, the queue selector at +20h, the state bytes at +2Eh, +33h and
// +34h and the window region WORD at +54h.
struct Window {
    U sibling = 0, child = 0, parent = 0, origin_x = 0, origin_y = 0;
    U queue = 0, state2e = 0, state33 = 0, state34 = 0, region = 0;
};

// USER. Every ancestor must carry +33h bit 10h; bit 20h is tolerated
// only on the window itself.
template <class E> U Reachable(E &e, U w)
{
    for (U p = w; p; p = e.Read(p).parent) {
        const auto s = e.Read(p);
        if (!(s.state33 & 0x10)) return 0;
        if ((s.state33 & 0x20) && p != w) return 0;
    }
    return 1;
}

// USER. `work` is the flag word passed to the children, `task` the
// current task the apply step receives. Answers zero when the accumulated
// region ran empty, which stops the sibling walk in the caller.
template <class E> U Walk(E &e, U w, U region, U hrgn, const Rect &bound, U flags, U task)
{
    const auto self = e.Read(w);
    if (self.region) flags |= Shaped;
    U work = flags | Trim;
    Rect rect{};
    e.WindowRect(w, rect, 1);
    // gates the whole intersection and classification block on
    // RDW_INVALIDATE; without it the walk goes straight to the apply step.
    if (flags & Invalidate) {
        if (!e.Intersect(rect, rect, bound)) {
            // a window marked 2Eh bit 4, or a caller region other than
            // the whole-window sentinel, gives up on this branch entirely.
            if ((self.state2e & 4) || hrgn != WholeWindow) return 1;
            flags &= ~U(Trim);
            work &= ~U(Trim);
        }
        if (hrgn > WholeWindow) {
            const U kind = e.Classify(hrgn, rect);  //
            if (!kind) return 1;
            if (kind == 1) {
                if (flags & Shaped) {
                    e.SetRectRgn(e.Scratch(2), rect);
                    for (U p = w; p; p = e.Read(p).parent) {
                        const U shape = e.Read(p).region;
                        if (shape) e.Combine(e.Scratch(2), e.Scratch(2), shape, And);
                    }
                    if (e.Combine(e.Scratch(2), e.Scratch(2), hrgn, And) == NullRegion) return 1;
                }
            } else if (kind == 2) {
                // asks KERNEL ordinal 354 about the owning queue's task;
                // without bit 1 of DX the caller's region degrades to the sentinel.
                if (!((e.TaskFlags(self.queue) >> 16) & 1)) hrgn = WholeWindow;
            }
        }
    }
    if (!(self.state33 & 2)) e.Apply(w, hrgn, task, flags);  //
    if (self.state2e & 8) e.Notify(w, rect);          //
    U children = self.child;
    if (children && !(self.state33 & 0x20) && !(flags & NoChildren)) {
        if ((flags & AllChildren) || !(self.state33 & 2)) {
            if (flags & Invalidate) work |= Frame | Erase;
            Rect inner{};
            const bool meet = e.Intersect(inner, rect, e.WindowBound(w));
            if (meet || (!(self.state2e & 4) && hrgn == WholeWindow)) {
                U carried = flags;
                for (U c = children; c; c = e.Read(c).sibling) {
                    if (!(e.Read(c).state33 & 0x10)) continue;
                    if (Walk(e, c, region, hrgn, inner, work, task)) continue;
                    // without RDW_INTERNALPAINT or RDW_NOINTERNALPAINT
                    // an exhausted region ends the whole walk.
                    if (!(carried & (InternalPaint | NoInternalPaint))) return 0;
                    carried &= 0xf3d2;
                    work &= 0xd3d2;
                }
                flags = carried;
            }
        }
    }
    if (self.state33 & 2) e.Apply(w, hrgn, task, flags);
    if (!(flags & Trim)) return 1;
    if ((self.state34 & 0x20) || !(self.state33 & 4)) return 1;
    if (!(flags & Validate)) {
        const U parent = self.parent;
        if (!parent) return 1;
        if (!(e.Read(parent).state33 & 2) && !(e.TaskFlags(self.queue) & 0x4000)) return 1;
    }
    e.SetRectRgn(e.Scratch(2), rect);
    if (flags & Shaped)
        for (U p = w; p; p = e.Read(p).parent) {
            const U shape = e.Read(p).region;
            if (shape) e.Combine(e.Scratch(2), e.Scratch(2), shape, And);
        }
    return e.Combine(region, region, e.Scratch(2), Diff) == NullRegion ? 0 : 1;
}

// USER. Chooses the window or client rectangle, builds the region the
// walk consumes and runs the immediate update the flags ask for.
template <class E> void Worker(E &e, U w, U region, U flags)
{
    Rect rect{};
    e.WindowRect(w, rect, (flags & Frame) ? 1 : 0);  //
    U use = region;
    if (flags & (Invalidate | Validate)) {
        if (region == WholeWindow) {
            use = e.Scratch(1);
            // the argument is one when RDW_FRAME is absent.
            e.WindowRegion(w, use, (flags & Frame) ? 0 : 1);  //
        }
        if ((flags & Invalidate) && !e.ClipToVisible(w, rect)) return;  //
    } else {
        use = e.Scratch(1);
        e.SetRectRgn(use, Rect{});
    }
    if ((flags & Invalidate) && (flags & Prepared) && e.Watching()) {
        Rect box{};
        e.RegionBox(use, box);
        e.Intersect(box, box, rect);
        e.Watch(w, box);
    }
    // Windows 98 passes the derived region as the walk's region and the caller's
    // own argument as its hrgn, so the sentinel stays visible to every child.
    const U task = e.CurrentTask();
    Walk(e, w, use, region, rect, flags, task);
    if (flags & UpdateNow) { e.UpdateNow(w, (flags & NoChildren) ? 0 : 1); return; }
    if (flags & EraseNow) {
        U mode = (flags & AllChildren) ? 8 : 4;
        if (flags & NoChildren) mode = 0;
        e.EraseNow(w, mode);
    }
}

// USER. Turns the caller's rect or region into one screen region in the
// first scratch handle and adds the internal flag 1000h.
template <class E> U Prepare(E &e, U w, U hrgn, const Rect *rect, U flags)
{
    if (!w) w = e.Desktop();
    if (!Reachable(e, w)) return 1;
    // Keep the caller's region handle unchanged. Windows narrows it to a word
    // because a Win16 region handle is one; a native GDI handle carries a
    // generation above the low word, and Body has already checked the type.
    U region = hrgn;
    if (flags & (Invalidate | Validate)) {
        if (!region) {
            region = WholeWindow;
            if (rect) {
                const U scratch = e.Scratch(0);
                Rect shifted = *rect;
                if (w != e.Desktop()) {
                    const auto s = e.Read(w);
                    shifted.left += S(s.origin_x); shifted.right += S(s.origin_x);
                    shifted.top += S(s.origin_y); shifted.bottom += S(s.origin_y);
                }
                e.SetRectRgn(scratch, shifted);
                region = scratch;
            }
        } else {
            if (region != WholeWindow) {
                e.Combine(e.Scratch(0), region, 0, Copy);
                region = e.Scratch(0);
            }
            if (w != e.Desktop()) {
                const auto s = e.Read(w);
                e.OffsetRgn(region, s.origin_x, s.origin_y);
            }
        }
    }
    Worker(e, w, region, flags | Prepared);
    return 1;
}

// The public entry requires a valid window handle.
// A non-null region must answer GDI object type 6, and the
// flag word may not carry a bit above 0FFFh.
template <class E> U Body(E &e, U hwnd, const Rect *rect, U hrgn, U flags)
{
    // Windows 98 passes the resolver's answer on without testing it; a zero WND
    // becomes the desktop. The resolver itself owns the bad-handle
    // case, so it never returns here with a diagnosis of its own.
    const U w = hwnd ? e.Resolve(hwnd) : 0;
    if (hrgn && e.ObjectType(hrgn) != RegionObject) { e.Error(hrgn, RegionError); return 0; }
    if (flags & 0xf000) { e.Error(flags, FlagError); return 0; }
    return Prepare(e, w, hrgn, rect, flags);
}
// The narrowing USER32 performs before the call. Native generation
// handles do not fit in a word, so the native path calls Body directly.
template <class E> U Entry(E &e, U hwnd, const Rect *rect, U hrgn, U flags)
{
    return Body(e, W(hwnd), rect, W(hrgn), W(flags));
}
}
