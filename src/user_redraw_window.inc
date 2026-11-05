// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside the anonymous namespace of user.cpp. The decisions come from
// win98_redraw_window.h; the providers bind them to native windows, native GDI
// regions and the per-window update region.
using RedrawCore = Win98RedrawWindow::Window;
using RedrawRect = Win98RedrawWindow::Rect;
// works on three scratch regions. RedrawWindow returns into the guest
// (WM_ERASEBKGND, WM_PAINT) for RDW_ERASENOW and RDW_UPDATENOW, and the guest
// may call RedrawWindow again from there. Each call therefore owns its three
// handles. One set is kept for the outermost frame so the ordinary path still
// creates no region; a nested frame makes its own and deletes them again.
unsigned redraw_scratch[3] = {};
bool redraw_scratch_busy = false;

struct RedrawScratchFrame
{
    unsigned handles[3] = {};
    bool cached = false;
    RedrawScratchFrame()
    {
        if (redraw_scratch_busy) return;
        redraw_scratch_busy = true; cached = true;
        for (unsigned i = 0; i < 3; ++i) handles[i] = redraw_scratch[i];
    }
    ~RedrawScratchFrame()
    {
        if (cached)
        {
            for (unsigned i = 0; i < 3; ++i) redraw_scratch[i] = handles[i];
            redraw_scratch_busy = false;
            return;
        }
        for (auto handle : handles) if (handle) Gdi::DeleteObject(handle);
    }
    unsigned Get(unsigned index)
    {
        if (!handles[index]) handles[index] = Gdi::CreateRegionRect(0, 0, 0, 0);
        return handles[index];
    }
};
void ReleaseRedrawScratch()
{
    for (auto &handle : redraw_scratch) { if (handle) Gdi::DeleteObject(handle); handle = 0; }
    redraw_scratch_busy = false;
}

Gdi::RECT ScreenRect(const Window &w, bool frame)
{
    const auto p = Origin(w);
    if (frame) return {p.x, p.y, p.x + w.width, p.y + w.height};
    const auto client = Client(w);
    return {p.x + ClientLeft(w), p.y + ClientTop(w),
            p.x + ClientLeft(w) + client.right, p.y + ClientTop(w) + client.bottom};
}

struct RedrawEnvironment
{
    void *context = nullptr;
    RedrawImmediate immediate = nullptr;
    RedrawScratchFrame *frame = nullptr;
    unsigned prepared = 0, error_code = 0, error_object = 0;

    RedrawCore Read(unsigned h)
    {
        RedrawCore r;
        auto *w = Find(h);
        if (!w) return r;
        r.parent = w->parent;
        r.child = w->first;
        r.sibling = w->next;
        const auto p = Origin(*w);
        r.origin_x = unsigned(p.x + ClientLeft(*w)) & 65535u;
        r.origin_y = unsigned(p.y + ClientTop(*w)) & 65535u;
        r.queue = w->thread;
        // Byte 33h is the top byte of the style: bit 10h the visibility every
        // ancestor must carry, bit 2 WS_CLIPCHILDREN (children are left out
        // unless RDW_ALLCHILDREN, and the window applies after them) and bit
        // 4 WS_CLIPSIBLINGS (the trim step). With one screen for all windows
        // the children of a parent without WS_CLIPCHILDREN are covered by its
        // paint and have to be invalid with it.
        r.state33 = (w->visible ? 0x10 : 0) | ((w->style >> 24) & 0x06);
        r.region = w->region;
        return r;
    }
    unsigned Desktop() { return GetDesktopWindow(); }
    unsigned Scratch(unsigned index) { return frame->Get(index); }

    unsigned CurrentTask() { return CurrentProcess(); }
    unsigned Resolve(unsigned hwnd) { return IsWindow(hwnd) ? hwnd : 0; }
    // Native GDI answers the Win32 object type; the original expects the Win16
    // region type 6.
    unsigned ObjectType(unsigned h) { return Gdi::GetObjectType(h) == 8 ? 6u : 0u; }
    void Error(unsigned object, unsigned code) { error_object = object; error_code = code; }
    void WindowRect(unsigned h, RedrawRect &out, unsigned kind)
    {
        auto *w = Find(h);
        if (!w) { out = RedrawRect{}; return; }
        const auto r = ScreenRect(*w, kind != 0);
        out = RedrawRect{r.left, r.top, r.right, r.bottom};
    }
    RedrawRect WindowBound(unsigned h)
    {
        auto *w = Find(h);
        if (!w) return RedrawRect{};
        const auto r = ScreenRect(*w, false);
        return RedrawRect{r.left, r.top, r.right, r.bottom};
    }
    bool Intersect(RedrawRect &out, const RedrawRect &a, const RedrawRect &b)
    {
        Gdi::RECT result{};
        const Gdi::RECT first{a.left, a.top, a.right, a.bottom}, second{b.left, b.top, b.right, b.bottom};
        if (!Win98::IntersectRect(&result, &first, &second)) { out = RedrawRect{}; return false; }
        out = RedrawRect{result.left, result.top, result.right, result.bottom};
        return true;
    }
    // Windows 98 answers 0 for "nothing here", 1 for a partial overlap and 2 for
    // the case the original resolves through the owning task. Native USER has
    // no second case; it reports 0 or 1 from the region bounds.
    unsigned Classify(unsigned hrgn, const RedrawRect &r)
    {
        Gdi::RECT bounds{};
        if (!Gdi::RegionBounds(hrgn, &bounds)) return 0;
        Gdi::RECT meet{};
        const Gdi::RECT rect{r.left, r.top, r.right, r.bottom};
        return Win98::IntersectRect(&meet, &bounds, &rect) ? 1u : 0u;
    }
    void SetRectRgn(unsigned h, const RedrawRect &r)
    {
        const auto fresh = Gdi::CreateRegionRect(r.left, r.top, r.right, r.bottom);
        if (!fresh) return;
        Gdi::CombineRegions(h, fresh, 0, 5);
        Gdi::DeleteObject(fresh);
    }
    unsigned Combine(unsigned dst, unsigned a, unsigned b, unsigned mode)
    {
        return unsigned(Gdi::CombineRegions(dst, a, b, mode));
    }
    void OffsetRgn(unsigned h, unsigned dx, unsigned dy)
    {
        Gdi::OffsetRegionObject(h, short(dx), short(dy));
    }
    // KERNEL.354 reports the owning task. Native USER keeps the caller's region
    // (DX bit 1) and never takes the parent-queue branch (AX bit 4000h).
    unsigned TaskFlags(unsigned) { return 0x10000; }
    void Apply(unsigned h, unsigned hrgn, unsigned, unsigned flags)
    {
        auto *w = Find(h);
        if (!w) return;
        if(flags&Win98RedrawWindow::InternalPaint){w->internal_paint=true;w->wait_paint_observed=false;}
        else if(flags&Win98RedrawWindow::NoInternalPaint)w->internal_paint=false;
        if (!(flags & (Win98RedrawWindow::Invalidate | Win98RedrawWindow::Validate))) return;
        const bool frame = (flags & Win98RedrawWindow::Frame) != 0;
        const auto screen_rect = ScreenRect(*w, frame);
        const int dx = -(Origin(*w).x + ClientLeft(*w)), dy = -(Origin(*w).y + ClientTop(*w));
        Gdi::RECT whole{screen_rect.left + dx, screen_rect.top + dy,
                        screen_rect.right + dx, screen_rect.bottom + dy};
        Gdi::RegionRectanglesCopy region;
        const Gdi::RECT *pieces=&whole;
        unsigned count=1;
        if(hrgn!=Win98RedrawWindow::WholeWindow && prepared)
        {
            if(region.Read(prepared,dx,dy)){pieces=region.rectangles;count=region.count;}
            // USER promotes a failed update-region allocation
            // or copy to the whole window. Never turn it into no damage.
            else if(!(flags&Win98RedrawWindow::Invalidate)){error_code=8;return;}
        }
        const bool erase = (flags & Win98RedrawWindow::Erase) != 0;
        const auto client = Client(*w);
        for (unsigned i = 0; i < count; ++i)
        {
            if (flags & Win98RedrawWindow::Invalidate) InvalidateOne(h, &pieces[i], erase);
            else ValidateRect(h, &pieces[i]);
            // Invalidate the frame with RDW_FRAME, including child traversal.
            // The owning thread draws it on the next WM_NCPAINT/BeginPaint;
            // otherwise frame-only controls can disappear after parent
            // erasure.
            const auto &p = pieces[i];
            const Gdi::RECT outer={-ClientLeft(*w),-ClientTop(*w),w->width-ClientLeft(*w),w->height-ClientTop(*w)};
            Gdi::RECT touched{};
            // Parent invalidation can carry a rectangle outside this child.
            // Such a rectangle must not schedule a nonclient/control paint.
            if(!Win98::IntersectRect(&touched,&p,&outer))continue;
            // RDW_FRAME owes a frame message even for a client-only rectangle.
            if ((flags & Win98RedrawWindow::Invalidate) && frame)
            {
                w->frame_message_owed = true;
                w->frame_update_pending = true;
                w->frame_update.Add(touched);
                w->wait_paint_observed = false;
            }
            if ((flags & Win98RedrawWindow::Invalidate) && frame && ControlContent(*w) &&
                p.left < p.right && p.top < p.bottom &&
                (p.left < client.left || p.top < client.top || p.right > client.right || p.bottom > client.bottom))
            {
                w->frame_owed = true;
                w->needs_paint = true;
                dirty = true;
            }
        }
        if ((flags & Win98RedrawWindow::Validate) && (flags & Win98RedrawWindow::NoErase))
            w->erase_pending = false;
        // NOFRAME suppresses WM_NCPAINT even when the supplied validation
        // rectangle misses the frame. Its pending paint survives a partial
        // validation; BeginPaint consumes it with an empty client region.
        if ((flags & Win98RedrawWindow::Validate) && (flags & Win98RedrawWindow::NoFrame))
        {
            w->frame_message_owed = false;
            if (hrgn == Win98RedrawWindow::WholeWindow)
            { w->frame_update_pending = false; w->frame_update.Clear(); }
        }
    }
    void Notify(unsigned, const RedrawRect &) {}
    void WindowRegion(unsigned h, unsigned region, unsigned client)
    {
        auto *w = Find(h);
        if (!w) return;
        const auto r = ScreenRect(*w, client == 0);
        SetRectRgn(region, RedrawRect{r.left, r.top, r.right, r.bottom});
    }
    bool ClipToVisible(unsigned h, RedrawRect &r)
    {
        if (!IsVisible(h)) return false;
        for (unsigned p = Find(h) ? Find(h)->parent : 0; p; p = Find(p) ? Find(p)->parent : 0)
        {
            auto *a = Find(p);
            if (!a) break;
            const auto area = ScreenRect(*a, false);
            if (!Intersect(r, r, RedrawRect{area.left, area.top, area.right, area.bottom})) return false;
        }
        return true;
    }
    unsigned Watching() { return 0; }
    void RegionBox(unsigned h, RedrawRect &out)
    {
        Gdi::RECT bounds{};
        Gdi::RegionBounds(h, &bounds);
        out = RedrawRect{bounds.left, bounds.top, bounds.right, bounds.bottom};
    }
    void Watch(unsigned, const RedrawRect &) {}
    void UpdateNow(unsigned h, unsigned recurse) { if (immediate) immediate(context, h, 1, recurse); }
    void EraseNow(unsigned h, unsigned mode) { if (immediate) immediate(context, h, 2, mode); }
};
