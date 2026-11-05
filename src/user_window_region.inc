// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside namespace User. Native objects replace the USER heap WND and
// the GDI region handles; the decisions come from win98_window_region.h.
// Included inside the anonymous namespace of user.cpp.
using WindowRegionCore = Win98WindowRegion::Window;
unsigned desktop_region = 0;

// The original stores the region in screen coordinates (it offsets the region by
// the window rectangle) and its SetWindowPos path keeps it there. Native USER
// re-offsets lazily instead; that move step is a native provider.
bool SyncWindowRegion(Window &w)
{
    if (!w.region) return true;
    const auto p = Origin(w);
    if (p.x == w.region_x && p.y == w.region_y) return true;
    if (!Gdi::OffsetRegionObject(w.region, p.x - w.region_x, p.y - w.region_y)) return false;
    w.region_x = p.x; w.region_y = p.y; return true;
}
// Intersects a DC's system clip with the window region. dx/dy translate from
// screen coordinates into the DC's own space.
bool RegionClip(Gdi::HDC dc, Window &w, int dx, int dy)
{
    if (!w.region) return true;
    if (!SyncWindowRegion(w)) return false;
    Gdi::RegionRectanglesCopy region;
    if(!region.Read(w.region,dx,dy))return false;
    return Gdi::IntersectClipRegion(dc,region.rectangles,region.count)!=0;
}
bool RegionHit(const Window &w, int screen_x, int screen_y)
{
    if (!w.region) return true;
    auto &mutable_window = const_cast<Window &>(w);
    if (!SyncWindowRegion(mutable_window)) return false;
    return Gdi::RegionContainsPoint(w.region, screen_x, screen_y);
}
bool RegionHit(HWND h, int screen_x, int screen_y)
{
    auto *w = Find(h); return !w || RegionHit(*w, screen_x, screen_y);
}
void FreeWindowRegion(Window &w)
{
    if (w.region) Gdi::DeleteObject(w.region);
    if (w.region) GeometryChanged();
    w.region = 0; w.region_x = w.region_y = 0;
}

// Providers for the original bodies. Window objects are native HWNDs, regions
// are native GDI handles and the task is the owning process.
struct RegionEnvironment
{
    HWND handle = 0;
    void *position_context = nullptr;
    RegionPosition position = nullptr;
    unsigned defer_flags = 0, defer_region = 0, error_code = 0, error_object = 0;
    WindowRegionCore Read(unsigned h)
    {
        WindowRegionCore r; auto *w = Find(h); if (!w) return r;
        const auto p = Origin(*w);
        r.region = w->region; r.task = WindowProcess(h);
        r.left = unsigned(p.x) & 65535u; r.top = unsigned(p.y) & 65535u;
        return r; // state33/state39 stay zero: native USER has no borrowed region.
    }
    void Write(unsigned h, const WindowRegionCore &v)
    {
        auto *w = Find(h); if (!w) return;
        // Deletion is the core's explicit Delete step; this only stores.
        w->region = v.region; GeometryChanged();
        const auto p = Origin(*w); w->region_x = p.x; w->region_y = p.y;
    }
    unsigned Desktop() { return User::Desktop; }
    unsigned Resolve(unsigned hwnd) { return IsWindow(hwnd) ? hwnd : 0; }
    // GDI.462 answers the Win16 object type; native GDI reports Win32
    // OBJ_REGION (8) for the same object.
    unsigned ObjectType(unsigned region)
    { return Gdi::GetObjectType(region) == 8 ? Win98WindowRegion::RegionObject : 0; }
    void Error(unsigned object, unsigned code) { error_object = object; error_code = code; }
    unsigned CurrentTask() { return CurrentProcess(); }
    // One native input queue per process; foreign-queue rejection uses it.
    // The +1 keeps the standalone process 0 distinguishable from "no task".
    unsigned Queue(unsigned task) { return task + 1; }
    unsigned BeginDefer(unsigned) { return 1; }
    unsigned Defer(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned flags)
    { defer_flags = flags; return 1; }
    void DeferRegion(unsigned, unsigned value) { defer_region = value; }
    unsigned EndDefer(unsigned, unsigned);
    void Delete(unsigned region) { Gdi::DeleteObject(region); }
    unsigned ClassRegion(unsigned) { return 0; } // No original class-region record natively.
    unsigned BorrowedRegion(unsigned) { return 0; }
    unsigned Offset(unsigned region, unsigned dx, unsigned dy)
    { return Gdi::OffsetRegionObject(region, short(dx), short(dy)) ? 1u : 0u; }
    void SetOwner(unsigned, unsigned) {}      // Native GDI has no object owner.
    void MakePrivate(unsigned, unsigned) {}   // and no private-object flag.
    unsigned Combine(unsigned destination, unsigned first, unsigned second, unsigned mode)
    { return unsigned(Gdi::CombineRegions(destination, first, second, mode)); }
    unsigned DesktopRegion();
    unsigned UserInstance() { return 0; }
    unsigned Undefined() { return 0; } // Unreachable: the desktop rejects SetWindowRgn.
};
unsigned RegionEnvironment::DesktopRegion()
{
    if (!desktop_region)
        desktop_region = Gdi::CreateRegionRect(0, 0, settings.Word(0) ? settings.Word(0) : 800,
                                               settings.Word(2) ? settings.Word(2) : 600);
    return desktop_region;
}
unsigned RegionEnvironment::EndDefer(unsigned, unsigned)
{
    if(position)return position(position_context,handle,defer_region,defer_flags);
    if (defer_region) Win98WindowRegion::Apply(*this, handle, defer_region);
    auto *w = Find(handle); if (!w) return 0;
    // SWP_FRAMECHANGED without SWP_NOREDRAW repaints frame and client.
    if (!(defer_flags & Win98WindowRegion::NoRedraw))
    {
        const auto client = Client(*w);
        InvalidateRect(handle, &client, true);
        dirty = true;
    }
    return 1;
}
