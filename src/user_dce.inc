// SPDX-License-Identifier: GPL-2.0-or-later
// USER's DCE cache. Included by src/user.cpp.
//
// Win98 USER keeps a list of DCEs (USER GetDCEx, ReleaseDC).
// A window of an ordinary class gets a free cache DC on every GetDC and
// BeginPaint, reset to the default attributes; two leases held at the same
// time are two DCs with their own state. CS_OWNDC (20h) gives the window one
// DC for its lifetime, CS_CLASSDC (40h) one DC for all windows of the class;
// both keep their attributes between leases and only get their origin and
// visible region set again.
//
// Every DCE draws on the one screen surface, the way a DC on DISPLAY does:
// the origin is the client corner of its window, the clip is the window's
// visible region (src/user_visible.inc). There is no surface per window.
bool Release(Window &,Gdi::HDC,bool);
namespace {
constexpr unsigned OwnDC = 0x20, ClassDC = 0x40;
struct ClassDCEntry { unsigned record = 0; Gdi::HDC dc = 0; HWND target = 0, owner = 0; ClassDCEntry *next=nullptr; };
ClassDCEntry *class_dcs=nullptr;
struct CacheDCEntry {Gdi::HDC dc;Window *target;CacheDCEntry *next;unsigned process=0,thread=0;};
CacheDCEntry *cache_dcs=nullptr;
bool ReserveLease(Window &w)
{
    if(w.leases<w.lease_capacity)return true;
    if(w.lease_capacity>~0u/2)return false;
    const unsigned capacity=w.lease_capacity?w.lease_capacity*2:8;
    if(sizeof(Window::Lease)>size_t(-1)/capacity)return false;
    auto *more=new(std::nothrow) Window::Lease[capacity];
    if(!more)return false;
    for(unsigned i=0;i<w.leases;++i)more[i]=w.lease[i];
    delete[] w.lease;w.lease=more;w.lease_capacity=capacity;return true;
}
struct DCRegionEntry { Gdi::HDC dc=0; unsigned region=0; bool intersect=false; HWND window=0; bool frame=false; DCRegionEntry *next=nullptr; };
DCRegionEntry *dc_regions=nullptr;
DCRegionEntry *DCRegion(Gdi::HDC dc) { for(auto *entry=dc_regions;entry;entry=entry->next)if(entry->dc==dc)return entry;return nullptr; }
void DropDCRegion(Gdi::HDC dc) {
    for(auto **link=&dc_regions;*link;link=&(*link)->next)if((*link)->dc==dc){
        auto *entry=*link;*link=entry->next;Gdi::DeleteObject(entry->region);delete entry;return;
    }
}
bool ApplyDCRegion(const Window &w,Gdi::HDC dc) {
    const auto *entry=DCRegion(dc);if(!entry||Find(entry->window)!=&w)return true;
    Gdi::RegionRectanglesCopy parts;
    const auto origin=Origin(w);
    if(!parts.Read(entry->region,-origin.x-(entry->frame?0:ClientLeft(w)),
                                -origin.y-(entry->frame?0:ClientTop(w))))return false;
    if(entry->intersect)return Gdi::IntersectClipRegion(dc,parts.rectangles,parts.count)!=0;
    for(unsigned i=0;i<parts.count;++i){const auto&r=parts.rectangles[i];
        if(!Gdi::ExcludeClipRect(dc,r.left,r.top,r.right,r.bottom))return false;}
    return true;
}
void ApplyPaintClips(const Window &w,Gdi::HDC dc){
    for(unsigned i=0;i<w.leases;++i)if(w.lease[i].dc==dc&&w.lease[i].paint_clip)
        Gdi::IntersectClipRegion(dc,w.lease[i].paint_clip->rectangles,w.lease[i].paint_clip->count);
}
Gdi::HDC ScreenDC()
{
    unsigned count = 0;
    const unsigned *table = ScreenTable(count);
    return count ? Gdi::CreateSurfaceDC(ScreenBitmapView(), table, count) : Gdi::CreateSurfaceDC(ScreenBitmapView());
}
ClassDCEntry *ClassEntry(const Window &w)
{
    if (!w.class_record) return nullptr;
    for (auto *e=class_dcs;e;e=e->next) if (e->record == w.class_record) return e;
    return nullptr;
}
ClassDCEntry *AllocateClassDC(HWND h,const Window &w)
{
    auto *e=new(std::nothrow) ClassDCEntry;
    if(!e)return nullptr;
    e->dc=ScreenDC();if(!e->dc){delete e;return nullptr;}
    e->record=w.class_record;e->owner=e->target=h;
    e->next=class_dcs;class_dcs=e;return e;
}
// USER allocates persistent DCEs when the window is created. Later changes
// to GCL_STYLE select among those DCEs; they do not allocate an own DC.
void InitializeWindowDCs(HWND h,Window &w)
{
    if(w.class_style&OwnDC)w.own_dc=ScreenDC();
    else if((w.class_style&ClassDC)&&!ClassEntry(w))AllocateClassDC(h,w);
}
bool Held(const Window &w, Gdi::HDC dc, unsigned except = ~0u)
{
    for (unsigned i = 0; i < w.leases; ++i) if (i != except && w.lease[i].dc == dc) return true;
    return false;
}
}
// The DC belongs to this window: one of its cache DCs, or the class DC while
// it is set on this window.
bool OwnsDC(const Window &w, Gdi::HDC dc)
{
    if (!dc) return false;
    if(w.own_dc==dc)return true;
    for(auto *e=cache_dcs;e;e=e->next)if(e->dc==dc&&e->target==&w)return true;
    if (const auto *e = ClassEntry(w)) return e->dc == dc && e->target && Find(e->target) == &w;
    return false;
}
// WindowFromDC: a cache DC names its window while it is held, an own or
// class DC always (USER reads the hwnd of the DCE).
bool DCWindow(const Window &w, Gdi::HDC dc)
{
    if (!OwnsDC(w, dc)) return false;
    return Held(w, dc) || (w.class_style & (OwnDC | ClassDC));
}
void FreeWindowDCs(Window &w)
{
    FreeHostSurface(w);
    for (unsigned i = 0; i < w.leases; ++i) { delete w.lease[i].paint_clip; w.lease[i].paint_clip = nullptr; }
    delete[] w.lease;w.lease=nullptr;w.leases=w.lease_capacity=0;
    for(auto **link=&cache_dcs;*link;){
        auto *e=*link;
        if(e->target!=&w){link=&e->next;continue;}
        DropDCRegion(e->dc);Gdi::DeleteDC(e->dc);*link=e->next;delete e;
    }
    if(w.own_dc){DropDCRegion(w.own_dc);Gdi::DeleteDC(w.own_dc);w.own_dc=0;}
    auto *e = ClassEntry(w);
    if (!e) return;
    const bool was_target=e->target && Find(e->target)==&w;
    if(was_target)e->target=0;
    // The last window of the class takes the class DC with it; otherwise the
    // DC waits with an empty clip for the next window of the class.
    for (const auto &other : windows)
        if (&other != &w && other.used && other.class_record == w.class_record) {
            if(was_target){Gdi::ResetSystemClip(e->dc);EmptyClip(e->dc);}
            return;
        }
    DropDCRegion(e->dc);
    Gdi::DeleteDC(e->dc);
    auto **link=&class_dcs;while(*link!=e)link=&(*link)->next;*link=e->next;delete e;
}
// The visible region of a window DC: its origin and the visible region of the
// client area with the window region, empty while the window is locked (USER
// through SetDCOrg and the visrgn of the DCE).
bool VisibleClip(HWND h, Window &w, Gdi::HDC dc, bool frame = false)
{
    if (host_window_surfaces) Gdi::ResizeSurfaceDC(dc, HostBitmap(h));
    const auto at = Origin(w);
    const int ox = at.x + (frame?0:ClientLeft(w)), oy = at.y + (frame?0:ClientTop(w));
    Gdi::SetDeviceOrigin(dc, ox, oy, false);
    auto *visible = new RectList;
    // A screen DC covers the desktop including its children. GetDC(NULL)
    // reaches this path with DCX_WINDOW, without the desktop class style.
    if (h == Desktop && frame) {
        visible->Append(WindowScreenRect(w));
        if (locked_window && locked_window != Desktop)
            if (const auto *locked = Find(locked_window))
                visible->Subtract(WindowScreenRect(*locked));
    } else WindowVisible(h, frame?WindowScreenRect(w):ClientScreenRect(w), false, *visible);
    const bool locked = Locked(h);
    for (unsigned k = 0; k < visible->n; ++k) USER_TRACE("VISIBLE hwnd=%u %d,%d,%d,%d\n", h, visible->r[k].left, visible->r[k].top, visible->r[k].right, visible->r[k].bottom);
    visible->Offset(-ox, -oy);
    const bool okay = Gdi::IntersectClipRegion(dc, visible->r, visible->n) != 0;
    delete visible;
    if (!okay || !RegionClip(dc, w, -ox, -oy)) return false;
    if (locked) EmptyClip(dc);
    return true;
}
// A held DC whose window moved, changed size or got covered gets its visible
// region again, as USER marks every DCE of such a window dirty and rebuilds
// the visrgn on the next use. A paint lease keeps its update clip.
void RefreshDC(HWND h, Window &w, Gdi::HDC dc, const RectRegion *paint_clip, bool frame = false)
{
    Gdi::ResetSystemClip(dc);
    if (!VisibleClip(h, w, dc,frame)) return;
    ApplyDCRegion(w,dc);
    if (paint_clip) Gdi::IntersectClipRegion(dc, paint_clip->rectangles, paint_clip->count);
}
void RefreshWindowDCs()
{
    auto refresh = [](HWND h, Window &w) {
        for (unsigned i = 0; i < w.leases; ++i) RefreshDC(h, w, w.lease[i].dc, w.lease[i].paint_clip,w.lease[i].frame);
        if ((w.class_style & OwnDC) && w.own_dc && !Held(w, w.own_dc)) RefreshDC(h, w, w.own_dc, nullptr);
        if (auto *e = ClassEntry(w)) if (e->target == h && !Held(w, e->dc)) RefreshDC(h, w, e->dc, nullptr);
    };
    if (desktop.used) refresh(Desktop, desktop);
    for (unsigned i = 0; i < MaxWindows; ++i)
        if (windows[i].used && (windows[i].leases || windows[i].own_dc || (windows[i].class_style & ClassDC)))
            refresh(Handle(i), windows[i]);
}
Gdi::HDC Acquire(HWND h, Window &w, bool paint, unsigned selection = ~0u, bool frame = false)
{
    if (!screen) return 0;
    SettleGeometry();
    if(!ReserveLease(w))return 0;
    Gdi::HDC dc = 0;
    if(selection==~0u)selection=w.class_style&(OwnDC|ClassDC);
    const bool keep = selection != 0;
    if (selection & OwnDC) {
        if (!w.own_dc) {
            w.own_dc = ScreenDC();
            if (!w.own_dc) return 0;
        }
        dc = w.own_dc;
    }
    else if (selection & ClassDC) {
        auto *e = ClassEntry(w);
        if (!e)e=AllocateClassDC(h,w);
        if (!e) return 0;
        if(e->target!=h)if(auto *region=DCRegion(e->dc))region->window=0;
        e->target = h;
        dc = e->dc;
    } else {
        CacheDCEntry *available=nullptr,*fifth=nullptr;
        unsigned held=0;
        for(auto *e=cache_dcs;e;e=e->next){
            if(!Held(*e->target,e->dc)){
                if(!available||e->target==&w)available=e;
            }else if(e->process==active_process&&e->thread==active_thread&&++held==5)fifth=e;
        }
        // USER records the fifth held cache DCE of this task.
        // Only if no free DCE exists does release and reuse that one.
        if(!available&&fifth){
            auto &old=*fifth->target;
            for(unsigned i=0;i<old.leases;++i)if(old.lease[i].dc==fifth->dc){
                Release(old,fifth->dc,old.lease[i].paint);break;
            }
            available=fifth;
        }
        if(!available){
            // USER -> 52CE grows the shared cache if no DCE is free.
            available=new(std::nothrow) CacheDCEntry{};
            if(!available)return 0;
            available->dc=ScreenDC();
            if(!available->dc){delete available;return 0;}
            available->next=cache_dcs;cache_dcs=available;
        }
        available->target=&w;dc=available->dc;
        available->process=active_process;available->thread=active_thread;
        // USER moves every selected entry to the list head.
        if(cache_dcs!=available){
            auto **link=&cache_dcs;while(*link!=available)link=&(*link)->next;
            *link=available->next;available->next=cache_dcs;cache_dcs=available;
        }
    }
    // A cache DC starts each lease with the default attributes and no saved
    // state (SaveDC answers 1, as on a fresh DCE); ReleaseDC takes back what
    // the caller selected. An own or class DC keeps everything but its
    // visible region.
    if (keep) Gdi::ResetSystemClip(dc);
    else Gdi::ResetSurfaceDC(dc);
    // Collect drawing bounds while saved-pixel records exist, including
    // LockWindowUpdate, CS_SAVEBITS windows and popup menus. DrawingChanged
    // uses these bounds.
    if (locked_window || AnySaved()) Gdi::SetBoundsRect(dc, nullptr, 0x8007);
    if (!VisibleClip(h, w, dc,frame) || !ApplyDCRegion(w,dc)) return 0;
    ApplyPaintClips(w,dc);
    w.lease[w.leases++] = {dc, keep ? 0 : 1, paint, nullptr, false, frame};
    return dc;
}
bool SetDCRegionBody(HWND h,Gdi::HDC dc,unsigned region,bool intersect)
{
    auto *w=Find(h);if(!w)return false;
    if(!OwnsDC(*w,dc))return false;
    bool frame=false;for(unsigned i=0;i<w->leases;++i)if(w->lease[i].dc==dc)frame=w->lease[i].frame;
    auto *entry=DCRegion(dc);
    // Switching a class DC to another window resets the clip but does not
    // take ownership of, or delete, the former window's region.
    if(!region && entry && entry->window!=h)return true;
    if(entry&&entry->region==region){entry->intersect=intersect;entry->frame=frame;}
    else {
        DCRegionEntry *next=nullptr;
        if(region){next=new(std::nothrow) DCRegionEntry{dc,region,intersect,h,frame};if(!next)return false;}
        DropDCRegion(dc);
        if(next){next->next=dc_regions;dc_regions=next;}
    }
    Gdi::ResetSystemClip(dc);
    return VisibleClip(h,*w,dc,frame)&&ApplyDCRegion(*w,dc);
}
bool Release(Window &w, Gdi::HDC dc, bool paint)
{
    if (!dc) return false;
    unsigned at = w.leases;
    while (at && !(w.lease[at - 1].dc == dc && w.lease[at - 1].paint == paint)) --at;
    if (!at) return (w.class_style & (OwnDC | ClassDC)) && OwnsDC(w,dc);
    const auto lease = w.lease[at - 1];
    // Collect attempted drawing during the lock before resetting the DC.
    LockCheckDC(&w == &desktop ? Desktop : Handle(unsigned(&w - windows)), dc);
    // USER resets a cache DC on release (unless DCX_NORESETATTRS):
    // saved states the caller left open go, then the attributes.
    if (lease.level && !Held(w, dc, at - 1)) { DropDCRegion(dc); Gdi::RestoreDC(dc, 1); Gdi::ResetSurfaceDC(dc); }
    // EndPaint releases with fEndPaint set, and
    // then drops the update region BeginPaint intersected (DCX_INTERSECTRGN)
    // from an own or class DC as well: deletes the clip region and
    // invalidates the visible region. A program that keeps drawing through
    // its own DC between paints (Visual Basic forms) draws on the
    // whole visible window again, not only where the last paint was.
    else if ((paint || lease.paint_clip) && !lease.level) {
        const HWND h = &w == &desktop ? Desktop : Handle(unsigned(&w - windows));
        Gdi::ResetSystemClip(dc);
        VisibleClip(h, w, dc);
    }
    delete lease.paint_clip;
    for (unsigned i = at; i < w.leases; ++i) w.lease[i - 1] = w.lease[i];
    w.lease[--w.leases] = {};
    if (!lease.level && (paint || lease.paint_clip)) { ApplyDCRegion(w,dc); ApplyPaintClips(w,dc); }
    if (lease.caret_hidden) ShowCaretFor(&w == &desktop ? Desktop : Handle(unsigned(&w - windows)));
    if (paint) dirty.Mark();
    return true;
}

// Read and clear collected bounds, intersecting them with the DCE window
// rectangle. Only DCs beneath the locked window contribute to its saved-pixel
// record.
void LockCheckDC(HWND h, Gdi::HDC dc)
{
    if (!locked_window || !dc) return;
    Gdi::RECT drawn{};
    if (!(Gdi::GetBoundsRect(dc, &drawn, 0x8001) & 2)) return;
    const HWND owner = h ? h : Desktop;
    if (!Locked(owner)) return;
    const auto *w = Find(owner), *locked = Find(locked_window);
    if (!w || !locked) return;
    const Gdi::RECT window = WindowScreenRect(*w), spb = WindowScreenRect(*locked);
    Gdi::RECT inside{}, cut{};
    if (Win98::IntersectRect(&inside, &drawn, &window) && Win98::IntersectRect(&cut, &inside, &spb)) LockSum(cut);
}
// Inspect all nonempty DCEs, including held leases, private DCs and class
// DCs.
void LockCheckAll()
{
    auto check = [](HWND h, Window &w) {
        for (unsigned i = 0; i < w.leases; ++i) LockCheckDC(h, w.lease[i].dc);
        if ((w.class_style & OwnDC) && w.own_dc && !Held(w, w.own_dc)) LockCheckDC(h, w.own_dc);
        if (auto *e = ClassEntry(w)) if (e->target == h && !Held(w, e->dc)) LockCheckDC(h, e->dc);
    };
    if (desktop.used) check(Desktop, desktop);
    for (unsigned i = 0; i < MaxWindows; ++i) if (windows[i].used) check(Handle(i), windows[i]);
}
// Start collecting DCE bounds for the first saved-pixel record; stop after
// the last is removed.
void LockBounds(bool enable)
{
    const unsigned flags = enable ? 0x8007u : 0x8008u;
    for(auto *e=cache_dcs;e;e=e->next)Gdi::SetBoundsRect(e->dc,nullptr,flags);
    auto set = [&](Window &w) { if(w.own_dc)Gdi::SetBoundsRect(w.own_dc,nullptr,flags); };
    if (desktop.used) set(desktop);
    for (auto &w : windows) if (w.used) set(w);
    for (auto *e=class_dcs;e;e=e->next) Gdi::SetBoundsRect(e->dc, nullptr, flags);
}
