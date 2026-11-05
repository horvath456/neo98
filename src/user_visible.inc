// SPDX-License-Identifier: GPL-2.0-or-later
// Visible regions for src/user.cpp. Every window draws into one screen
// surface, clipped to its visible region; newly exposed areas are repainted.
// Clip each window to the screen and all ancestor client areas. Subtract
// visible siblings in front for top-level windows and WS_CLIPSIBLINGS
// children; subtract visible children for WS_CLIPCHILDREN.
// SettleGeometry preserves previously visible pixels. For a moving window,
// translate the old region, intersect it with the new region and copy that
// portion. Invalidate newly owned areas with background erasure, matching
// Windows 98 SetWindowPos.

// A non-overlapping rectangle list in screen coordinates. Like a GDI region,
// it grows without a fixed rectangle limit.
struct RectList
{
    // Most visible regions have at most four rectangles. Keep those local.
    Gdi::RECT small[4];
    Gdi::RECT *r = small;
    unsigned n = 0, capacity = 4;
    RectList() = default;
    RectList(const RectList &other) { *this = other; }
    RectList &operator=(const RectList &other)
    {
        if (this == &other) return *this;
        n = 0;
        Reserve(other.n);
        for (unsigned i = 0; i < other.n; ++i) r[i] = other.r[i];
        n = other.n;
        return *this;
    }
    ~RectList() { if (r != small) delete[] r; }
    void Reserve(unsigned want)
    {
        if (want <= capacity) return;
        unsigned grown = capacity ? capacity : 16;
        while (grown < want) grown *= 2;
        auto *more = new Gdi::RECT[grown];
        for (unsigned i = 0; i < n; ++i) more[i] = r[i];
        if (r != small) delete[] r;
        r = more; capacity = grown;
    }
    void Clear() { n = 0; }
    void Append(const Gdi::RECT &a)
    {
        if (a.left >= a.right || a.top >= a.bottom) return;
        if (n == capacity) Reserve(n + 1);
        r[n++] = a;
    }
    void Clip(const Gdi::RECT &c)
    {
        unsigned k = 0;
        for (unsigned i = 0; i < n; ++i)
        {
            Gdi::RECT a = r[i];
            if (a.left < c.left) a.left = c.left;
            if (a.top < c.top) a.top = c.top;
            if (a.right > c.right) a.right = c.right;
            if (a.bottom > c.bottom) a.bottom = c.bottom;
            if (a.left < a.right && a.top < a.bottom) r[k++] = a;
        }
        n = k;
    }
    // Subtract a rectangle, keeping up to four fragments in the same list.
    void Subtract(const Gdi::RECT &cut)
    {
        if (cut.left >= cut.right || cut.top >= cut.bottom) return;
        const unsigned count = n;
        unsigned k = 0;
        // Append fragments beyond the original input, so Reserve may grow
        // the array without overwriting rectangles we have yet to visit.
        // Then close the gap. Order stays untouched rectangles, fragments.
        for (unsigned i = 0; i < count; ++i)
        {
            const auto a = r[i];
            if (cut.left >= a.right || cut.right <= a.left || cut.top >= a.bottom || cut.bottom <= a.top) {
                r[k++] = a;
                continue;
            }
            if (count == 1) n = 0;
            const int top = cut.top > a.top ? cut.top : a.top, bottom = cut.bottom < a.bottom ? cut.bottom : a.bottom;
            Append({a.left, a.top, a.right, top});
            Append({a.left, bottom, a.right, a.bottom});
            Append({a.left, top, cut.left > a.left ? cut.left : a.left, bottom});
            Append({cut.right < a.right ? cut.right : a.right, top, a.right, bottom});
        }
        if (count != 1 || k) {
            for (unsigned i = count; i < n; ++i) r[k++] = r[i];
            n = k;
        }
    }
    void Subtract(const RectList &other) { for (unsigned i = 0; i < other.n; ++i) Subtract(other.r[i]); }
    void Intersect(const RectList &a, const RectList &b)
    {
        Clear();
        for (unsigned i = 0; i < a.n; ++i)
            for (unsigned j = 0; j < b.n; ++j)
            {
                Gdi::RECT c{};
                if (Win98::IntersectRect(&c, &a.r[i], &b.r[j])) Append(c);
            }
    }
    void Offset(int dx, int dy) { for (unsigned i = 0; i < n; ++i) { r[i].left += dx; r[i].right += dx; r[i].top += dy; r[i].bottom += dy; } }
    bool Touches(const Gdi::RECT &c) const
    {
        for (unsigned i = 0; i < n; ++i)
            if (!(c.left >= r[i].right || c.right <= r[i].left || c.top >= r[i].bottom || c.bottom <= r[i].top)) return true;
        return false;
    }
};

// A window displayed on screen: visible and not minimized. PaintWindow does
// not draw minimized icons.
HWND host_hidden_window = 0;
bool Shows(const Window &w) { return w.visible && !(w.style & 0x20000000) && &w != Find(host_hidden_window); }
bool ShowsTree(HWND h)
{
    for (unsigned n = 0; h && h != Desktop && n < MaxWindows; ++n)
    {
        const auto *w = Find(h);
        if (!w || !Shows(*w)) return false;
        h = w->parent;
    }
    return true;
}
Gdi::RECT WindowScreenRect(const Window &w)
{
    const auto at = Origin(w);
    return {at.x, at.y, at.x + w.width, at.y + w.height};
}
Gdi::RECT ClientScreenRect(const Window &w)
{
    const auto at = Origin(w);
    const auto c = Client(w);
    return {at.x + ClientLeft(w), at.y + ClientTop(w), at.x + ClientLeft(w) + c.right, at.y + ClientTop(w) + c.bottom};
}
void ScreenSize(int &width, int &height);
bool SameRect(const Gdi::RECT &a, const Gdi::RECT &b)
{ return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
// Window coverage in screen pixels: intersect the frame or client rectangle
// with the window region.
void WindowShape(const Window &w, bool frame, RectList &out)
{
    out.Clear();
    out.Append(frame ? WindowScreenRect(w) : ClientScreenRect(w));
    if (!w.region || !out.n) return;
    auto &mutable_window = const_cast<Window &>(w);
    RectList region;
    const unsigned count = SyncWindowRegion(mutable_window) ? Gdi::RegionRectangles(w.region, nullptr, 0, 0, 0) : 0;
    if (count) {
        region.Reserve(count);
        region.n = Gdi::RegionRectangles(w.region, region.r, count, 0, 0);
        if (region.n > count) region.n = count;
    }
    RectList bounded;
    bounded.Intersect(out, region);
    out = bounded;
}
// Intersect the rectangle list with a window's coverage.
void IntersectShape(RectList &out, const Window &w, bool frame)
{
    if (!w.region) { out.Clip(frame ? WindowScreenRect(w) : ClientScreenRect(w)); return; }
    RectList shape, cut;
    WindowShape(w, frame, shape);
    cut.Intersect(out, shape);
    out = cut;
}
// Subtract a window's coverage from the rectangle list.
void SubtractShape(RectList &out, const Window &w)
{
    if (!w.region) { out.Subtract(WindowScreenRect(w)); return; }
    RectList shape;
    WindowShape(w, true, shape);
    out.Subtract(shape);
}
// Compute the visible part of area in screen coordinates. Intersect the
// window region; subtract visible children for WS_CLIPCHILDREN or children.
// At each ancestor, subtract earlier visible siblings as required by
// WS_CLIPSIBLINGS and intersect the ancestor's client and window regions. Do
// not subtract WS_EX_TRANSPARENT children or siblings. Use actual
// window-region shapes.
void WindowVisible(HWND h, const Gdi::RECT &area, bool children, RectList &out)
{
    USER_PROFILE(UserVisible);
    out.Clear();
    const auto *self = Find(h);
    if (!self || !ShowsTree(h)) return;
    out.Append(area);
    int width = 0, height = 0;
    ScreenSize(width, height);
    out.Clip({0, 0, width, height});
    if (self->region && out.n) IntersectShape(out, *self, true);
    auto takes_out = [](const Window &w) { return Shows(w) && !(w.extended & 0x20u); };
    if (children || (self->style & 0x02000000u))
    {
        // Children occupy only their parent's client area. Pixels extending
        // over the parent frame, such as the taskbar clock over
        // TrayNotifyWnd's inset edge, still belong to the parent.
        const auto client = ClientScreenRect(*self);
        RectList frame;
        frame = out;
        frame.Subtract(client);
        out.Clip(client);
        for (HWND c = self->first; c && out.n;)
        {
            const auto *child = Find(c); if (!child) break;
            if (takes_out(*child)) SubtractShape(out, *child);
            c = child->next;
        }
        for (unsigned k = 0; k < frame.n; ++k) out.Append(frame.r[k]);
    }
    HWND current = h;
    for (unsigned guard = 0; current && current != Desktop && out.n && guard < MaxWindows; ++guard)
    {
        const auto *w = Find(current);
        if (!w) { out.Clear(); return; }
        const HWND container = Container(*w);
        const auto *parent = Find(container);
        if (!parent) break;
        if ((container == Desktop && !host_window_surfaces) || (container != Desktop && (w->style & 0x04000000u)))
            for (HWND s = parent->first; s && s != current && out.n;)
            {
                const auto *sibling = Find(s); if (!sibling) break;
                if (takes_out(*sibling)) SubtractShape(out, *sibling);
                s = sibling->next;
            }
        if (container != Desktop) IntersectShape(out, *parent, false);
        current = container;
    }
}

// Screen surface pixels (src/user_screen.inc).
DibEng::Bitmap ScreenBitmapView();
const unsigned *ScreenTable(unsigned &count);
// Read all source rectangles before writing any destination, allowing
// arbitrary overlap. Copy all windows moved during one geometry pass together
// so one copy cannot overwrite another's source.
struct CopyBatch
{
    struct Move { Gdi::RECT target; int dx, dy; };
    Move *moves = nullptr;
    unsigned count = 0, capacity = 0;
    void Add(const RectList &targets, int dx, int dy)
    {
        for (unsigned i = 0; i < targets.n; ++i)
        {
            if (count == capacity)
            {
                const unsigned grown = capacity ? capacity * 2 : 64;
                auto *more = new Move[grown];
                for (unsigned k = 0; k < count; ++k) more[k] = moves[k];
                delete[] moves; moves = more; capacity = grown;
            }
            moves[count++] = {targets.r[i], dx, dy};
        }
    }
    void Run()
    {
        const auto s = ScreenBitmapView();
        const int bytes = s.depth / 8;
        if (!s.bits || !count || bytes <= 0) return;
        Gdi::GuardScreenBits(s.bits, true);
        size_t total = 0;
        for (unsigned i = 0; i < count; ++i)
            total += size_t(moves[i].target.right - moves[i].target.left) * size_t(moves[i].target.bottom - moves[i].target.top) * bytes;
        auto *buffer = new unsigned char[total ? total : 1];
        size_t at = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto &t = moves[i].target;
            const size_t row = size_t(t.right - t.left) * bytes;
            for (int y = t.top; y < t.bottom; ++y, at += row)
                memcpy(buffer + at, s.bits + (long)(y - moves[i].dy) * s.stride + (long)(t.left - moves[i].dx) * bytes, row);
        }
        at = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto &t = moves[i].target;
            const size_t row = size_t(t.right - t.left) * bytes;
            for (int y = t.top; y < t.bottom; ++y, at += row)
                memcpy(s.bits + (long)y * s.stride + (long)t.left * bytes, buffer + at, row);
        }
        delete[] buffer;
    }
    ~CopyBatch() { delete[] moves; }
};
void CopyScreenRects(const RectList &targets, int dx, int dy)
{
    CopyBatch batch;
    batch.Add(targets, dx, dy);
    batch.Run();
}
// Each window's previous visible region excluding visible children, plus its
// position and size.
struct ShownRecord
{
    HWND handle = 0, parent = 0;
    unsigned z_order = 0;
    Gdi::RECT *rects = nullptr;
    unsigned count = 0;
    Gdi::RECT window{}, client{};
    // Pixels saved beneath a CS_SAVEBITS window or popup menu, using its
    // rectangle when it appeared.
    unsigned char *saved = nullptr;
    Gdi::RECT saved_rect{};
    int saved_stride = 0;
    bool saved_valid = false;
    RectList saved_region;
    // A child with WS_EX_TRANSPARENT: its pixels lie on its parent's region
    // and its siblings', which never lost them.
    bool transparent = false;
};
ShownRecord shown[MaxWindows + 1];
unsigned geometry_serial = 1, settled_serial = 0;
bool settling = false;
// A settle that only takes note: WM_SETREDRAW switches WS_VISIBLE without
// SetWindowPos (USER over ), so nothing is copied, restored or
// invalidated; the pixels stay as they are (SetRedrawVisible).
bool settle_quietly = false;
// A window's position, size, visibility, order, or shape changed.
void GeometryChanged() { ++geometry_serial; }
void DropSaved(ShownRecord &s) { delete[] s.saved; s.saved = nullptr; s.saved_valid = false; s.saved_region.Clear(); }
// Saved pixels use the framebuffer's format. A depth change at the same
// size retains their regions and converts the backups with the framebuffer.
// A dialog closing during ChangeDisplaySettings can then restore its old
// background, including the transparent controls behind it.
void ReformatSaved(int old_depth, const unsigned *old_colors, int depth, const unsigned *colors)
{
    for (auto &s : shown)
    {
        if (!s.saved || !s.saved_valid) continue;
        const int width = s.saved_rect.right - s.saved_rect.left;
        const int height = s.saved_rect.bottom - s.saved_rect.top;
        const int stride = ((width * depth + 31) / 32) * 4;
        auto *bits = new unsigned char[size_t(stride) * height]{};
        const DibEng::Bitmap before{s.saved, width, height, s.saved_stride, old_depth};
        const DibEng::Bitmap after{bits, width, height, stride, depth};
        const auto source = Gdi::CreateSurfaceDC(before, old_depth <= 8 ? old_colors : nullptr, old_depth <= 8 ? 1u << old_depth : 0);
        const auto target = Gdi::CreateSurfaceDC(after, depth <= 8 ? colors : nullptr, depth <= 8 ? 1u << depth : 0);
        const bool okay = source && target && Gdi::BitBlt(target, 0, 0, width, height, source, 0, 0, 0xcc0020);
        if (target) Gdi::DeleteDC(target);
        if (source) Gdi::DeleteDC(source);
        delete[] s.saved; s.saved = bits; s.saved_stride = stride; s.saved_valid = okay;
    }
}
void ResetShown()
{
    for (auto &s : shown) { delete[] s.rects; DropSaved(s); s = ShownRecord{}; }
    GeometryChanged();
}
HWND ShownHandle(unsigned index) { return index == MaxWindows ? Desktop : Handle(index); }
Window *ShownWindow(unsigned index) { return index == MaxWindows ? &desktop : (windows[index].used ? &windows[index] : nullptr); }
bool SavesBits(HWND h, const Window &w);
// Whether h equals ancestor or descends from it. Destroyed windows retain
// only their previous geometry record. previous also selects the old
// hierarchy for live windows, such as before SetParent.
bool ShownInside(HWND h, HWND ancestor, bool previous = false)
{
    for (unsigned depth = 0; h && h != Desktop && depth < 64; ++depth)
    {
        if (h == ancestor) return true;
        if (!previous)
            if (const auto *w = Find(h)) { h = w->parent; continue; }
        const unsigned index = (h & 0x3ff) - 1;
        if (index >= MaxWindows || shown[index].handle != h) return false;
        h = shown[index].parent;
    }
    return false;
}
// Invalidate only the changed part of the saved background. Dropping the
// entire backup when one covered control changes also loses pixels behind
// transparent group boxes, which a WS_CLIPCHILDREN parent cannot repaint.
int CompareWindowOrder(HWND parent, HWND first, HWND second, unsigned depth = 0)
{
    const auto *p = Find(parent);
    if (!p || depth == 64) return 0;
    for (HWND h = p->first; h;)
    {
        const auto *w = Find(h); if (!w) break;
        const int found = CompareWindowOrder(h, first, second, depth + 1);
        if (found) return found;
        h = w->next;
    }
    return parent == first ? 1 : parent == second ? -1 : 0;
}
void UnderlyingChanged(HWND h, const Gdi::RECT &screen_area, unsigned above_order = ~0u)
{
    for (unsigned i = 0; i <= MaxWindows; ++i)
    {
        auto &s = shown[i];
        if (!s.saved_valid || !s.handle || s.handle == h) continue;
        if (s.z_order >= above_order) continue;
        // The window and its children draw over the saved pixels.
        if (ShownInside(h, s.handle)) continue;
        // Windows 98 walks to the common parent, then through its siblings.
        // Drawing ABOVE a saved window does not change its background.
        // Use the live tree for newly shown windows, the remembered order
        // when a destroyed window has already been unlinked.
        if (above_order == ~0u)
        {
            if (Find(h) && Find(s.handle))
            {
                if (CompareWindowOrder(Desktop, s.handle, h) != 1) continue;
            }
            else
            {
                const unsigned index = (h & 1023) - 1;
                if (index < MaxWindows && shown[index].handle == h &&
                    s.z_order >= shown[index].z_order) continue;
            }
        }
        const auto &r = s.saved_rect;
        if (!(screen_area.left >= r.right || screen_area.right <= r.left || screen_area.top >= r.bottom || screen_area.bottom <= r.top))
        {
            if (s.saved_valid) USER_TRACE("SAVED dropped hwnd=%u by=%u area=%d,%d,%d,%d\n", s.handle, h, screen_area.left, screen_area.top, screen_area.right, screen_area.bottom);
            // Older saved states predate the partial-validity region.
            if (!s.saved_region.n) s.saved_region.Append(s.saved_rect);
            s.saved_region.Subtract(screen_area);
            s.saved_valid = s.saved_region.n != 0;
        }
    }
}
bool AnySaved() { for (const auto &s : shown) if (s.saved_valid) return true; return false; }
// Preserve the previous front-to-back order even after DestroyWindow has
// unlinked a window. Children are in front of their parent's own surface.
void RememberShownOrder(HWND parent, unsigned &order, unsigned depth = 0)
{
    const auto *p = Find(parent);
    if (!p || depth == 64) return;
    for (HWND h = p->first; h;)
    {
        const auto *w = Find(h); if (!w) break;
        RememberShownOrder(h, order, depth + 1);
        const unsigned index = (h & 1023) - 1;
        if (index < MaxWindows && shown[index].handle == h) shown[index].z_order = order++;
        h = w->next;
    }
}
void InvalidateOne(HWND h, const Gdi::RECT *rect, bool erase);
void RefreshWindowDCs();
void PaintWindow(HWND h, Window &w);
unsigned NativeSignature(HWND h, const Window &w);
// Compare old and new visible regions for every displayed window. This pass
// makes no guest callbacks.
void SettleGeometry()
{
    if (settling || settled_serial == geometry_serial || !screen) return;
    // Geometry reconciliation moves and restores screen pixels. Defer it
    // while another thread locks the primary surface; the triggering call,
    // such as SetWindowPos, still returns. Resume after Unlock.
    if (ScreenLockedElsewhere(CurrentProcess(), CurrentThread())) return;
    USER_PROFILE(UserGeometry);
    settling = true;
    settled_serial = geometry_serial;
    // An EDIT of USER redraws itself the moment its state changes (focus,
    // selection, text); here PaintWindow does it at the next frame. Before a
    // change of geometry erases what it now covers, those pending redraws
    // run, so they come first as in the original. Otherwise the Address edit
    // of the General page lost its focus to the tab, the property sheet put
    // the page to the bottom (COMCTL32 SetWindowPos HWND_BOTTOM) and the tab
    // erased its body, and the edit then drew itself alone on the empty tab
    // until the Connections page was made.
    for (unsigned i = 0; i < MaxWindows; ++i)
    {
        auto &w = windows[i];
        if (!w.used || !w.edit || !w.parent || (w.style & 0x04000000u) || w.native_invalid) continue;
        const HWND h = Handle(i);
        if (ShowsTree(h) && NativeSignature(h, w) != w.native_signature) PaintWindow(h, w);
    }
    const auto surface = ScreenBitmapView();
    struct Fresh { RectList *list = nullptr; bool shows = false; };
    static Fresh fresh[MaxWindows + 1];
    // 1. The new regions.
    for (unsigned i = 0; i <= MaxWindows; ++i)
    {
        fresh[i] = Fresh{};
        auto *w = ShownWindow(i);
        const HWND h = ShownHandle(i);
        if (!w || (i < MaxWindows && !ShowsTree(h))) continue;
        fresh[i].list = new RectList;
        fresh[i].shows = true;
        WindowVisible(h, WindowScreenRect(*w), true, *fresh[i].list);
    }
    // 2. Discard saved pixels beneath windows that disappear or move.
    auto record_of = [&](unsigned i) -> ShownRecord * {
        auto &s = shown[i];
        return s.handle && s.handle == ShownHandle(i) && ShownWindow(i) ? &s : nullptr; };
    for (unsigned i = 0; i < MaxWindows; ++i)
    {
        auto &s = shown[i];
        if (!s.handle) continue;
        const bool same = record_of(i) != nullptr;
        const auto *w = same ? ShownWindow(i) : nullptr;
        const bool changed = !same || !fresh[i].shows || !SameRect(s.window, WindowScreenRect(*w));
        // USER also checks the other SPBs when a saved window goes.
        // Windows 98 tests their relative order: a window above this one
        // may have saved its pixels. Its backup is stale even if this
        // window has a backup of its own. McAfee hides its splash while
        // its error dialog still covers it.
        if (changed) UnderlyingChanged(s.handle, s.window, s.saved ? s.z_order : ~0u);
    }
    for (unsigned i = 0; i < MaxWindows; ++i)
        if (fresh[i].shows && !record_of(i))
            if (auto *w = ShownWindow(i); w && !SavesBits(ShownHandle(i), *w)) UnderlyingChanged(ShownHandle(i), WindowScreenRect(*w));
    // 3. Copy moving windows before restoring their old backgrounds.
    RectList *valid = new RectList, *old = new RectList;
    CopyBatch batch;
    for (unsigned i = 0; i <= MaxWindows && !settle_quietly; ++i)
    {
        auto *s = record_of(i);
        if (!s || !fresh[i].shows) continue;
        const auto *w = ShownWindow(i);
        const auto now = WindowScreenRect(*w), client = ClientScreenRect(*w);
        const int dx = now.left - s->window.left, dy = now.top - s->window.top;
        const bool resized = now.right - now.left != s->window.right - s->window.left ||
                             now.bottom - now.top != s->window.bottom - s->window.top ||
                             client.left - now.left != s->client.left - s->window.left ||
                             client.top - now.top != s->client.top - s->window.top ||
                             client.right - client.left != s->client.right - s->client.left ||
                             client.bottom - client.top != s->client.bottom - s->client.top;
        if ((!dx && !dy) || resized) continue;
        old->Clear();
        for (unsigned k = 0; k < s->count; ++k) old->Append(s->rects[k]);
        old->Offset(dx, dy);
        valid->Intersect(*old, *fresh[i].list);
        batch.Add(*valid, dx, dy);
    }
    batch.Run();
    // 4. Restore the exposed part of a hidden, destroyed or moved SPB.
    RectList *restored = new RectList;
    for (unsigned i = 0; i < MaxWindows && !settle_quietly; ++i)
    {
        auto &s = shown[i];
        if (!s.handle || !s.saved) continue;
        const bool gone = !record_of(i) || !fresh[i].shows;
        const auto *w = record_of(i) ? ShownWindow(i) : nullptr;
        const bool moved = w && fresh[i].shows && !SameRect(s.window, WindowScreenRect(*w));
        if (!gone && !moved) continue;
        USER_TRACE("SAVED %s hwnd=%u valid=%u\n", gone ? "restore" : "moved", s.handle, s.saved_valid ? 1u : 0u);
        if (s.saved_valid && surface.bits && surface.depth / 8 > 0)
        {
            // The saved image covers the full window rectangle. Restore only
            // the area last owned by the window and the children disappearing
            // with it, such as dialog controls.
            const int bytes = surface.depth / 8;
            if (!s.saved_region.n) s.saved_region.Append(s.saved_rect);
            Gdi::GuardScreenBits(surface.bits, true);
            for (unsigned j = 0; j < MaxWindows; ++j)
            {
                const auto &part = shown[j];
                if (!part.handle || (gone && record_of(j) && fresh[j].shows) || !ShownInside(part.handle, s.handle)) continue;
                for (unsigned k = 0; k < part.count; ++k)
                {
                    for (unsigned v = 0; v < s.saved_region.n; ++v)
                    {
                        Gdi::RECT r{};
                        if (!Win98::IntersectRect(&r, &part.rects[k], &s.saved_region.r[v])) continue;
                        RectList targets;
                        targets.Append(r);
                        // Another member of EndDeferWindowPos may have moved
                        // into this old rectangle, even below this SPB.
                        for (unsigned c = 0; c < batch.count && targets.n; ++c)
                            targets.Subtract(batch.moves[c].target);
                        if (moved)
                        {
                            // Repaint only the exposed region. Keep the moved window's destination,
                            // its children and windows above it untouched.
                            for (unsigned k = 0; k < MaxWindows && targets.n; ++k)
                                if (fresh[k].shows &&
                                    (ShownInside(ShownHandle(k), s.handle) ||
                                     CompareWindowOrder(Desktop, ShownHandle(k), s.handle) == 1))
                                    targets.Subtract(*fresh[k].list);
                        }
                        for (unsigned t = 0; t < targets.n; ++t)
                        {
                            const auto &target = targets.r[t];
                            for (int y = target.top; y < target.bottom; ++y)
                                memcpy(surface.bits + (long)y * surface.stride + (long)target.left * bytes,
                                       s.saved + (long)(y - s.saved_rect.top) * s.saved_stride + (long)(target.left - s.saved_rect.left) * bytes,
                                       size_t(target.right - target.left) * bytes);
                            restored->Append(target);
                        }
                    }
                }
            }
        }
        DropSaved(s);
    }
    // 5. Invalidate newly owned areas that were not preserved by translating
    // the old region.
    bool exposed_any = false;
    RectList *exposed = new RectList;
    for (unsigned i = 0; i <= MaxWindows && !settle_quietly; ++i)
    {
        if (!fresh[i].shows) continue;
        auto *w = ShownWindow(i);
        const HWND h = ShownHandle(i);
        const auto now = WindowScreenRect(*w), client = ClientScreenRect(*w);
        auto *s = record_of(i);
        *exposed = *fresh[i].list;
        if (s)
        {
            const int dx = now.left - s->window.left, dy = now.top - s->window.top;
            const bool resized = now.right - now.left != s->window.right - s->window.left ||
                                 now.bottom - now.top != s->window.bottom - s->window.top ||
                                 client.right - client.left != s->client.right - s->client.left ||
                                 client.bottom - client.top != s->client.bottom - s->client.top ||
                                 client.left - now.left != s->client.left - s->window.left ||
                                 client.top - now.top != s->client.top - s->window.top;
            if (!resized)
            {
                old->Clear();
                for (unsigned k = 0; k < s->count; ++k) old->Append(s->rects[k]);
                old->Offset(dx, dy);
                exposed->Subtract(*old);
            }
        }
        exposed->Subtract(*restored);
        USER_TRACE("SETTLE hwnd=%u known=%u fresh=%u exposed=%u client=%d,%d,%d,%d\n", h, s ? 1u : 0u, fresh[i].list->n, exposed->n, client.left, client.top, client.right, client.bottom);
        if (!exposed->n) continue;
        exposed_any = true;
        w->native_invalid = true;
        // Invalidate the client with erasure. Render repaints the frame
        // separately.
        const int ox = client.left, oy = client.top;
        for (unsigned k = 0; k < exposed->n; ++k)
        {
            Gdi::RECT c{};
            if (!Win98::IntersectRect(&c, &exposed->r[k], &client)) continue;
            const Gdi::RECT local{c.left - ox, c.top - oy, c.right - ox, c.bottom - oy};
            InvalidateOne(h, &local, true);
        }
        // A child's area also belongs to every ancestor lacking
        // WS_CLIPCHILDREN. Invalidate exposed areas there so the parent can
        // paint for children whose WM_ERASEBKGND/WM_PAINT do nothing. Newly
        // shown children also invalidate the corresponding visible parent
        // area; SetWindowPos erases it synchronously. user_events.c tests
        // STATIC and EDIT with and without WS_CLIPSIBLINGS.
        const Window *above = i < MaxWindows ? Find(w->parent) : nullptr;
        const bool parent_known = above && above >= windows && above < windows + MaxWindows &&
                                  record_of(unsigned(above - windows));
        // USER invalidates the remaining exposed region from the
        // positioning operation's parent, with RDW_ERASE|RDW_ALLCHILDREN.
        // A stationary child can be uncovered by a window outside its tree.
        // Propagate that exposure too, but subtract the previous coverage
        // of each complete subtree before going above it. Hiding a child
        // inside that subtree only owes its parent's paint, not a paint in
        // the grandparent. Bounds alone cannot distinguish these cases.
        const bool changed_bounds = s && (!SameRect(s->window, now) || !SameRect(s->client, client));
        if (i < MaxWindows && (w->style & 0xc0000000u) == 0x40000000u)
        {
            RectList propagated = *exposed;
            const bool stationary = s && !changed_bounds && parent_known;
            HWND branch = h;
            HWND up = w->parent;
            for (unsigned guard = 0; up && up != Desktop && guard < 64; ++guard)
            {
                auto *ancestor = Find(up);
                if (!ancestor || (ancestor->style & 0x02000000u)) break;
                if (stationary)
                    for (const auto &part : shown)
                        if (part.handle && part.count && ShownInside(part.handle, branch, true))
                            for (unsigned k = 0; k < part.count; ++k) propagated.Subtract(part.rects[k]);
                if (!propagated.n) break;
                const auto area = ClientScreenRect(*ancestor);
                for (unsigned k = 0; k < propagated.n; ++k)
                {
                    Gdi::RECT c{};
                    if (!Win98::IntersectRect(&c, &propagated.r[k], &area)) continue;
                    const Gdi::RECT local{c.left - area.left, c.top - area.top, c.right - area.left, c.bottom - area.top};
                    InvalidateOne(up, &local, true);
                }
                if ((ancestor->style & 0xc0000000u) != 0x40000000u) break;
                branch = up;
                up = ancestor->parent;
            }
        }
    }
    // 5b. A WS_EX_TRANSPARENT child that is hidden, destroyed or moved
    // leaves its old pixels on regions that never changed, because CalcVisRgn
    // does not take such a child out; the comparison above finds
    // nothing exposed. SetWindowPos treats the window as SWP_NOCOPYBITS
    // and invalidates the area it covered in its parent, with
    // RDW_ERASE and RDW_ALLCHILDREN, so the parent and the siblings below
    // repaint it. Windows Installer hides its transparent ActionData text
    // before every new action. The walk runs after the settle.
    // The same holds for such a child that appears or moves: SetWindowPos
    // keeps none of its bits (it empties the rectangle to copy),
    // and the region it now covers goes to the parent with RDW_ALLCHILDREN,
    // so the parent and the siblings below paint first and the
    // child draws over fresh pixels. Windows Installer shows its ActionText
    // again after every new text.
    struct Uncovered { HWND parent; Gdi::RECT area; };
    static Uncovered uncovered[64];
    unsigned uncovered_count = 0;
    for (unsigned i = 0; i < MaxWindows && !settle_quietly; ++i)
    {
        const auto &s = shown[i];
        if (!s.handle || !s.transparent || !s.parent) continue;
        const auto *w = record_of(i) ? ShownWindow(i) : nullptr;
        if (w && fresh[i].shows && SameRect(s.window, WindowScreenRect(*w))) continue;
        for (unsigned k = 0; k < s.count && uncovered_count < 64; ++k)
            uncovered[uncovered_count++] = {s.parent, s.rects[k]};
    }
    for (unsigned i = 0; i < MaxWindows && !settle_quietly; ++i)
    {
        if (!fresh[i].shows) continue;
        const auto *w = ShownWindow(i);
        if (!(w->extended & 0x20u) || (w->style & 0xc0000000u) != 0x40000000u || !w->parent) continue;
        const auto *s = record_of(i);
        if (s && SameRect(s->window, WindowScreenRect(*w))) continue;
        for (unsigned k = 0; k < fresh[i].list->n && uncovered_count < 64; ++k)
            uncovered[uncovered_count++] = {w->parent, fresh[i].list->r[k]};
    }
    // 6. Save the new regions. Newly visible CS_SAVEBITS windows save the
    // underlying pixels before painting.
    bool took_saved = false;
    for (unsigned i = 0; i <= MaxWindows; ++i)
    {
        auto &s = shown[i];
        const bool kept = record_of(i) != nullptr;
        if (!fresh[i].shows)
        {
            delete[] s.rects; DropSaved(s); s = ShownRecord{};
            continue;
        }
        auto *w = ShownWindow(i);
        const HWND h = ShownHandle(i);
        if (!kept) { delete[] s.rects; DropSaved(s); s = ShownRecord{}; }
        delete[] s.rects;
        s.handle = h;
        s.parent = i < MaxWindows ? w->parent : 0;
        s.count = fresh[i].list->n;
        s.rects = s.count ? new Gdi::RECT[s.count] : nullptr;
        for (unsigned k = 0; k < s.count; ++k) s.rects[k] = fresh[i].list->r[k];
        s.window = WindowScreenRect(*w);
        s.client = ClientScreenRect(*w);
        s.transparent = i < MaxWindows && (w->extended & 0x20u) && (w->style & 0xc0000000u) == 0x40000000u;
        if (!kept && i < MaxWindows && SavesBits(h, *w) && surface.bits && surface.depth / 8 > 0)
        {
            Gdi::RECT r = s.window;
            int width = 0, height = 0;
            ScreenSize(width, height);
            if (r.left < 0) r.left = 0;
            if (r.top < 0) r.top = 0;
            if (r.right > width) r.right = width;
            if (r.bottom > height) r.bottom = height;
            if (r.left < r.right && r.top < r.bottom)
            {
                const int bytes = surface.depth / 8;
                s.saved_stride = (r.right - r.left) * bytes;
                s.saved = new unsigned char[size_t(s.saved_stride) * size_t(r.bottom - r.top)];
                Gdi::GuardScreenBits(surface.bits, false);
                for (int y = r.top; y < r.bottom; ++y)
                    memcpy(s.saved + (long)(y - r.top) * s.saved_stride,
                           surface.bits + (long)y * surface.stride + (long)r.left * bytes, size_t(s.saved_stride));
                s.saved_rect = r;
                s.saved_valid = true;
                s.saved_region.Clear();
                s.saved_region.Append(r);
                took_saved = true;
                USER_TRACE("SAVED taken hwnd=%u rect=%d,%d,%d,%d\n", h, r.left, r.top, r.right, r.bottom);
            }
        }
    }
    unsigned order = 0;
    RememberShownOrder(Desktop, order);
    if (took_saved)
    {
        // Pending paints beneath a new saved image invalidate it. Before the
        // first desktop paint, saved pixels may still be black. Restoring
        // them later must not suppress the pending paint.
        auto pending = [](HWND h, const Window &w) {
            if (!ShowsTree(h)) return;
            const auto client = ClientScreenRect(w);
            for (unsigned k = 0; k < w.update.count; ++k) {
                const auto &r = w.update.rectangles[k];
                UnderlyingChanged(h, {client.left + r.left, client.top + r.top,
                                      client.left + r.right, client.top + r.bottom});
            }
        };
        pending(Desktop, desktop);
        for (unsigned i = 0; i < MaxWindows; ++i)
            if (windows[i].used) pending(Handle(i), windows[i]);
    }
    for (auto &f : fresh) { delete f.list; f.list = nullptr; }
    delete restored; delete valid; delete old; delete exposed;
    if (exposed_any) dirty = true;
    else dirty.Mark();
    RefreshWindowDCs();
    settling = false;
    for (unsigned k = 0; k < uncovered_count; ++k)
    {
        const auto *parent = Find(uncovered[k].parent);
        if (!parent || !ShowsTree(uncovered[k].parent)) continue;
        const auto client = ClientScreenRect(*parent);
        const auto &r = uncovered[k].area;
        const Gdi::RECT local{r.left - client.left, r.top - client.top, r.right - client.left, r.bottom - client.top};
        RedrawWindow(uncovered[k].parent, &local, 0, 0x85);
    }
}
