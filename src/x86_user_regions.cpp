// SPDX-License-Identifier: GPL-2.0-or-later
// The USER32 exports that hand a window's device context or its update region
// to the caller as a GDI region: GetWindowDC, GetDCEx, GetUpdateRgn,
// InvalidateRgn and ExcludeUpdateRgn. All five are word thunks into USER.EXE
// segment 6; the decisions below come from that code.
#include "x86_app.h"
#include "win98_redraw_window.h"
namespace X86 {
namespace {
// USER may produce arbitrarily fragmented update/visibility regions. Query
// the provider before allocating, and keep the snapshot local to this call.
struct RectangleSnapshot
{
    Gdi::RECT *parts = nullptr;
    unsigned count = 0;
    ~RectangleSnapshot() { delete[] parts; }
    bool Read(unsigned hwnd, unsigned (*read)(unsigned, Gdi::RECT *, unsigned))
    {
        unsigned capacity = 0;
        for (;;)
        {
            count = read(hwnd, parts, capacity);
            if (count <= capacity) return true;
            auto *grown = new(std::nothrow) Gdi::RECT[count];
            if (!grown) return false;
            delete[] parts;
            parts = grown;
            capacity = count;
        }
    }
};
// GDI object types Win98GdiObjectType::Type answers.
constexpr unsigned RegionType = 8, DeviceContextType = 3, MemoryContextType = 10;
// RGN_AND is 1, RGN_OR 2, RGN_DIFF 4 and RGN_COPY 5; the region complexity is
// NULLREGION 1, SIMPLEREGION 2 and COMPLEXREGION 3 with 0 for an error.
constexpr unsigned RegionOr = 2, RegionCopy = 5;
// USER rejects every DCX bit outside these two masks with error F005h.
// The low word allows DCX_WINDOW, DCX_CACHE, DCX_CLIPCHILDREN,
// DCX_CLIPSIBLINGS, DCX_PARENTCLIP, DCX_EXCLUDERGN, DCX_INTERSECTRGN and
// DCX_LOCKWINDOWUPDATE, the high word only DCX_USESTYLE.
constexpr unsigned DcxRejectLow = 0xfb04, DcxRejectHigh = 0xfffe;
constexpr unsigned DcxWindow = 0x0001, DcxClipChildren = 0x0008, DcxClipSiblings = 0x0010;
constexpr unsigned DcxParentClip = 0x0020, DcxExcludeRgn = 0x0040, DcxIntersectRgn = 0x0080;
constexpr unsigned DcxUseStyle = 0x00010000;
} // namespace

// USER. The flag and region checks are the original's; the clipping is
// the native providers. A window DC leases the screen DC with the origin on
// the window corner, a client DC is exactly User::GetDC; both carry the
// window's visible region.
unsigned Application::WindowDeviceContext(unsigned hwnd, unsigned region, unsigned flags)
{
    if (!User::IsWindow(hwnd))
    {
        last_error = 1400;
        return 0;
    }
    if (region && Gdi::GetObjectType(region) != RegionType)
    {
        // raises 6067h for a handle that is not a region object.
        last_error = 87;
        return 0;
    }
    // The Win32 entry accepts NORESETATTRS; Win98 ignores that hint.
    flags &= ~4u;
    if ((flags & DcxRejectLow) || ((flags >> 16) & DcxRejectHigh))
    {
        last_error = 87;   // raises F005h for a rejected flag bit
        return 0;
    }
    if (flags & DcxUseStyle)
    {
        // lets the window and class styles decide: WS_CLIPCHILDREN,
        // WS_CLIPSIBLINGS and CS_PARENTDC.
        // Keep USESTYLE for DCE selection below.
        const unsigned style = User::Style(hwnd);
        if (style & 0x02000000) flags |= DcxClipChildren;
        if (style & 0x04000000) flags |= DcxClipSiblings;
        if (User::ClassAccess(hwnd, -26, 0, 2) & 0x80) flags |= DcxParentClip;
    }
    const auto dc = User::GetDCEx(hwnd,flags);
    if (!dc) return 0;
    // The visible region follows the window's own WS_CLIPCHILDREN and
    // WS_CLIPSIBLINGS; the two DCX flags take children and siblings out on
    // top of it.
    RectangleSnapshot rectangles;
    const Gdi::POINT base = WindowClientOrigin(hwnd, (flags & DcxWindow) != 0);
    for (unsigned kind = DcxClipChildren; kind <= DcxClipSiblings; kind <<= 1)
    {
        if (!(flags & kind)) continue;
        if (!rectangles.Read(hwnd, kind == DcxClipChildren ?
                             User::VisibleChildRects : User::SiblingsAboveRects))
        {
            User::ReleaseDC(hwnd, dc);
            last_error = 8;
            return 0;
        }
        for (unsigned i = 0; i < rectangles.count; ++i)
        {
            const auto &part = rectangles.parts[i];
            Gdi::ExcludeClipRect(dc, part.left - base.x, part.top - base.y,
                                 part.right - base.x, part.bottom - base.y);
        }
    }
    if (flags & (DcxIntersectRgn | DcxExcludeRgn))
    {
        if (!User::SetDCRegion(hwnd, dc, region, (flags & DcxIntersectRgn) != 0))
        {
            User::ReleaseDC(hwnd, dc);
            last_error = 87;
            return 0;
        }
    }
    return dc;
}
// The device origin the DC above carries, so a screen rectangle can be turned
// into the DC's own coordinates.
Gdi::POINT Application::WindowClientOrigin(unsigned hwnd, bool frame)
{
    Gdi::RECT window = {};
    Gdi::POINT origin = {0, 0};
    if (!User::GetWindowRect(hwnd, &window)) return origin;
    if (frame) return {window.left, window.top};
    Gdi::POINT point = {0, 0};
    if (!User::ScreenToClient(hwnd, &point)) return {window.left, window.top};
    return {-point.x, -point.y};
}
// USER. The erase runs first, then the update region moves into the
// caller's region object in client coordinates and the complexity is the
// answer. An empty update region leaves an empty region and answers NULLREGION.
unsigned Application::UpdateRegionApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("GetUpdateRgn")
    {
        if (!User::IsWindow(a[0]))
        {
            last_error = 1400;
            return 0;
        }
        if (Gdi::GetObjectType(a[1]) != RegionType)
        {
            last_error = 87;
            return 0;
        }
        if (a[2] && User::GetUpdateRect(a[0], nullptr) && User::TakeFramePaint(a[0]))
            CallWindow(a[0], 0x85, 1, 0);
        if (a[2] && User::GetUpdateRect(a[0], nullptr) && User::NeedsErase(a[0]))
        {
            const unsigned token = User::EraseToken(a[0]);
            if (const auto dc = User::GetUpdateDC(a[0]))
            {
                const bool handled = CallWindow(a[0], 0x14, dc, 0) != 0;
                User::RememberErase(a[0], handled);
                if (handled || !EraseAgain()) User::Erased(a[0], token);
                User::ReleaseDC(a[0], dc);
            }
        }
        RectangleSnapshot rectangles;
        if (!rectangles.Read(a[0], User::UpdateRectangles))
        {
            last_error = 8;
            return 0;
        }
        const auto *parts = rectangles.parts;
        const unsigned count = rectangles.count;
        const auto accumulator = Gdi::CreateRegionRect(0, 0, 0, 0);
        if (!accumulator)
        {
            last_error = 8;
            return 0;
        }
        bool okay = true;
        for (unsigned i = 0; i < count && okay; ++i)
        {
            const auto piece = Gdi::CreateRegionRect(parts[i].left, parts[i].top,
                                                     parts[i].right, parts[i].bottom);
            if (!piece) { okay = false; break; }
            okay = Gdi::CombineRegions(accumulator, accumulator, piece, RegionOr) != 0;
            Gdi::DeleteObject(piece);
        }
        const unsigned kind = okay ? unsigned(Gdi::CombineRegions(a[1], accumulator, 0, RegionCopy)) : 0;
        Gdi::DeleteObject(accumulator);
        if (!okay) last_error = 8;
        return kind;
    }
    API_CASE("InvalidateRgn")
    {
        // The region argument travels to the RedrawWindow
        // worker with RDW_INVALIDATE and RDW_ERASE when bErase is set. A null
        // region means the whole window.
        if (!User::IsWindow(a[0]))
        {
            last_error = 1400;
            return 0;
        }
        if (a[1] && Gdi::GetObjectType(a[1]) != RegionType)
        {
            last_error = 87;
            return 0;
        }
        return RedrawWindowRegion(a[0], a[1], Win98RedrawWindow::Invalidate |
                                              (a[2] ? Win98RedrawWindow::Erase : 0u));
    }
    API_CASE("ValidateRgn")
    {
        // and Windows 98 take the same path with RDW_VALIDATE.
        if (!User::IsWindow(a[0]))
        {
            last_error = 1400;
            return 0;
        }
        if (a[1] && Gdi::GetObjectType(a[1]) != RegionType)
        {
            last_error = 87;
            return 0;
        }
        return RedrawWindowRegion(a[0], a[1], Win98RedrawWindow::Validate);
    }
    API_CASE("ExcludeUpdateRgn")
    {
        // No update region leaves the clip alone and answers the clip
        // box complexity; otherwise every update rectangle leaves the clip.
        const unsigned type = Gdi::GetObjectType(a[0]);
        if (type != DeviceContextType && type != MemoryContextType)
        {
            last_error = 1400;   // raises 6062h for a non-DC handle
            return 0;
        }
        if (!User::IsWindow(a[1]))
        {
            last_error = 1400;
            return 0;
        }
        RectangleSnapshot rectangles;
        if (!rectangles.Read(a[1], User::UpdateRectangles))
        {
            last_error = 8;
            return 0;
        }
        const auto *parts = rectangles.parts;
        const unsigned count = rectangles.count;
        if (!count)
        {
            // Windows 98 answers with GDI's own clip box call, which needs the
            // rectangle argument to report a type.
            Gdi::RECT box = {};
            return Gdi::GetClipBox(a[0], &box);
        }
        // USER combines the application clip and the update
        // region in screen units, offsets by -GetDCOrg, then SelectClipRgn.
        // ExcludeClipRect instead transforms logical coordinates, which is
        // wrong for a memory/foreign DC and for a non-default mapping mode.
        Gdi::HGDIOBJ clip = 0;
        if (!Gdi::CopyApplicationClip(a[0], clip)) return 0;
        Gdi::POINT origin = {};
        if (!Gdi::GetDeviceOrigin(a[0], &origin))
        {
            if (clip) Gdi::DeleteObject(clip);
            return 0;
        }
        if (!clip)
        {
            // Windows 98 copies USER's screen region when GDI.173 returns zero.
            clip = Gdi::CreateRegionRect(-origin.x, -origin.y,
                int(SystemMetric(0)) - origin.x, int(SystemMetric(1)) - origin.y);
            if (!clip) return 0;
        }
        const auto client = WindowClientOrigin(a[1], false);
        bool okay = true;
        for (unsigned i = 0; i < count && okay; ++i)
        {
            const auto &r = parts[i];
            const auto piece = Gdi::CreateRegionRect(r.left + client.x - origin.x,
                r.top + client.y - origin.y, r.right + client.x - origin.x,
                r.bottom + client.y - origin.y);
            if (!piece) { okay = false; break; }
            okay = Gdi::CombineRegions(clip, clip, piece, 4) != 0;
            Gdi::DeleteObject(piece);
        }
        if (okay) okay = Gdi::SelectClipRegion(a[0], clip);
        Gdi::DeleteObject(clip);
        if (!okay) return 0;
        Gdi::RECT box = {};
        return Gdi::GetClipBox(a[0], &box);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
// with a region argument. RedrawWindow itself is native already; this
// only supplies the whole-window sentinel the worker expects for a null region.
unsigned Application::RedrawWindowRegion(unsigned hwnd, unsigned region, unsigned flags)
{
    const auto immediate = [](void *p, unsigned window, unsigned kind, unsigned parameter) {
        static_cast<Application *>(p)->RedrawImmediate(window, kind, parameter);
    };
    const bool result = User::RedrawWindow(hwnd, nullptr, region, flags, this, immediate);
    if (!result && User::RedrawError()) last_error = 87;
    return result;
}
} // namespace X86
