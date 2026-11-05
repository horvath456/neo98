// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside namespace User, after the anonymous namespace.
unsigned SetWindowRegion(HWND h, unsigned region, bool redraw,void *context,RegionPosition position)
{
    RegionEnvironment e; e.handle = h;
    e.position_context=context;e.position=position;
    return Win98WindowRegion::Set(e, h, region, redraw ? 1u : 0u);
}
void ApplyWindowRegion(HWND h,unsigned region)
{
    if(!IsWindow(h))return;
    RegionEnvironment e;e.handle=h;Win98WindowRegion::Apply(e,h,region);
}
unsigned GetWindowRegion(HWND h, unsigned region)
{
    RegionEnvironment e; e.handle = h;
    if (auto *w = Find(h)) SyncWindowRegion(*w);
    return Win98WindowRegion::Get(e, h, region);
}
unsigned WindowRegion(HWND h) { auto *w = Find(h); return w ? w->region : 0; }
bool WindowRegionContains(HWND h, int screen_x, int screen_y) { return RegionHit(h, screen_x, screen_y); }
