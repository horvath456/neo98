// SPDX-License-Identifier: GPL-2.0-or-later
// The three GDI answers that the shared USER scroll worker needs and no other
// export of this port asks for. USER converts its rectangles with
// LPtoDP (GDI.99) and DPtoLP (GDI.67), and for a complex clip region it builds
// the device context's visible region out of InquireVisRgn (GDI.131) and
// GetClipRgn (GDI.173).

// LPtoDP and DPtoLP over the two corners of a rectangle, the shape the worker
// uses with a point count of two.
bool DeviceRect(HDC handle, RECT &rect)
{
    auto *c = DC(handle);
    if (!c) return false;
    const auto &s = c->current.mapping;
    const auto corner = Win98DcMap::Transform(s, {rect.left, rect.top}, false);
    const auto far_corner = Win98DcMap::Transform(s, {rect.right, rect.bottom}, false);
    rect = {corner.x, corner.y, far_corner.x, far_corner.y};
    return true;
}
bool LogicalRect(HDC handle, RECT &rect)
{
    auto *c = DC(handle);
    if (!c) return false;
    const auto &s = c->current.mapping;
    const auto corner = Win98DcMap::Transform(s, {rect.left, rect.top}, true);
    const auto far_corner = Win98DcMap::Transform(s, {rect.right, rect.bottom}, true);
    rect = {corner.x, corner.y, far_corner.x, far_corner.y};
    return true;
}
// USER: the clip state a scroll has to respect is the visible region of
// the device context intersected with the application's clip region. This port
// keeps exactly that product as the effective clip list of the DC, in surface
// units; the copy handed out moves into device units the same way GetClipRgn
// and GetRandomRgn move theirs.
HGDIOBJ ClipRegionObject(HDC handle)
{
    auto *c = DC(handle);
    if (!c) return 0;
    RegionSerial serial;
    if (!region_store.Initialize()) return 0;
    auto &s = c->current;
    const unsigned raw = s.clip_snapshot ? region_store.Clone(s.clip_snapshot->raw) : DefaultClip(s);
    if (!raw) return 0;
    Win98Region::Offset(region_store, raw,
                        unsigned(s.surface_origin.x - s.mapping.device_origin.x),
                        unsigned(s.surface_origin.y - s.mapping.device_origin.y));
    return PublishRegion(raw);
}
// USER preserve only the application clip around a monitor
// callback. Other DC attributes changed by the callback remain changed.
bool CopyApplicationClip(HDC handle,HGDIOBJ &region)
{
    region=0;auto *c=DC(handle);if(!c)return false;
    const auto &s=c->current;
    if(!s.application_snapshot)return true;
    RegionSerial serial;
    const unsigned raw=region_store.Clone(s.application_snapshot->raw);if(!raw)return false;
    Win98Region::Offset(region_store,raw,unsigned(s.surface_origin.x-s.mapping.device_origin.x),
                        unsigned(s.surface_origin.y-s.mapping.device_origin.y));
    region=PublishRegion(raw);return region!=0;
}
bool SelectClipRegion(HDC handle,HGDIOBJ region)
{
    auto *c=DC(handle);if(!c)return false;
    RegionSerial serial;
    const unsigned source=RawRegion(region);if(region&&!source)return false;
    unsigned copy=source?region_store.Clone(source):0;if(source&&!copy)return false;
    if(copy)Win98Region::Offset(region_store,copy,
        unsigned(c->current.mapping.device_origin.x-c->current.surface_origin.x),
        unsigned(c->current.mapping.device_origin.y-c->current.surface_origin.y));
    return ReplaceClip(c->current,copy,true);
}
