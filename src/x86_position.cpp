// SPDX-License-Identifier: GPL-2.0-or-later
// New native window layout batching. Z-order/activation flags remain limited.
// The SWP_NOMOVE/SWP_NOSIZE completion follows original USER.
#include "x86_app.h"
#include "win98_window_parent.h"
#include "win98_nonclient.h"
#include <string.h>
namespace X86
{
// Shared WINDOWPOS/NCCALCSIZE envelope: seven WINDOWPOS doublewords, three
// RECTs and the NCCALCSIZE_PARAMS pointer back to WINDOWPOS.
static constexpr unsigned PositionBlockBytes = 7 * 4 + 3 * 16 + 4;
// Deliver positioning messages through SendMessage on the owning thread,
// including WM_WINDOWPOSCHANGING, WM_NCCALCSIZE, WM_CHILDACTIVATE and
// WM_WINDOWPOSCHANGED. Windows 98 shares this envelope across processes;
// Neo98 mirrors it into the receiver's address space for the send and reads
// it back afterwards, as with COPYDATASTRUCT.
unsigned Application::PositionSend(unsigned hwnd, unsigned message, unsigned wp, unsigned lp, unsigned block)
{
    const unsigned owner = User::WindowProcess(hwnd);
    const unsigned running = User::CurrentProcess();
    // Same-thread sends use CallWindow directly for the common fast path.
    if ((!owner || owner == process_id) && (!running || running == process_id) &&
        (!User::WindowThread(hwnd) || User::WindowThread(hwnd) == ThreadId()))
        return CallWindow(hwnd, message, wp, lp);
    auto *other = owner && owner != process_id
                      ? static_cast<Application *>(User::ProcessContextOf(owner))
                      : nullptr;
    // A failed or exited process has no address space for the mirrored
    // envelope.
    if (!block || !other || other == this || other->state == Failed || other->state == Exited)
        return SendFromNative(hwnd, message, wp, lp);
    const unsigned mirror = other->Allocate(PositionBlockBytes);
    if (!mirror) { last_error = 8; return 0; }
    unsigned char bytes[PositionBlockBytes] = {};
    if (!Read(block, bytes, sizeof bytes)) { other->Free(mirror); last_error = 87; return 0; }
    // NCCALCSIZE_PARAMS ends with a pointer to WINDOWPOS in the same packet.
    // Redirect it to the mirrored structure.
    if (message == 0x83) memcpy(bytes + PositionBlockBytes - 4, &mirror, 4);
    if (!other->Store(mirror, bytes, sizeof bytes)) { other->Free(mirror); last_error = 8; return 0; }
    const unsigned result = SendFromNative(hwnd, message, wp, mirror + (lp - block));
    // Sending can run other threads, including receiver teardown. Revalidate
    // the receiver before reading back its memory; if it exited, leave the
    // sender's envelope unchanged.
    if (User::ProcessContextOf(owner) != other || other->state == Failed || other->state == Exited)
        return result;
    if (other->Read(mirror, bytes, sizeof bytes))
    {
        if (message == 0x83) memcpy(bytes + PositionBlockBytes - 4, &block, 4);
        Store(block, bytes, sizeof bytes);
    }
    other->Free(mirror);
    return result;
}
unsigned Application::PositionWindows(unsigned count, unsigned (*positions)[7],unsigned region_window,unsigned region)
{
    // USER (EndDeferWindowPos) with its worker. Per entry the
    // window first sees WM_WINDOWPOSCHANGING and may edit the WINDOWPOS; the
    // request is completed (SWP_NOMOVE/SWP_NOSIZE when nothing changes,
    // SWP_NOREDRAW for the invisible), and WM_NCCALCSIZE follows when the
    // size or the frame changes. Then all entries move and reorder, the first
    // entry without SWP_NOACTIVATE activates, and reports
    // WM_WINDOWPOSCHANGED to every entry that changed at all.
    if (count > 64) count = 64;
    struct Entry
    {
        unsigned hwnd, after, flags, block, original_flags;
        int x, y, width, height;
        Gdi::RECT old_window, old_client, new_client;
        bool dropped, calculated;
    } entries[64];
    unsigned candidate = 0;
    for (unsigned i = 0; i < count; ++i)
        if (!User::IsWindow(positions[i][0]))
        {
            last_error = 1400;
            return 0;
        }
    unsigned grouped[64][7], grouped_count = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned n = User::PrepareOwnedPositions(positions[i], grouped + grouped_count, 64 - grouped_count);
        if (!n) { last_error = 8; return 0; }
        grouped_count += n;
    }
    count = grouped_count;
    positions = grouped;
    for (unsigned i = 0; i < count; ++i)
    {
        auto &n = entries[i];
        const unsigned *a = positions[i];
        // USER32 MoveWindow/SetWindowPos/DeferWindowPos push WORD arguments
        // into USER. The 16-bit WINDOWPOS fields are signed, including sizes;
        // negative sizes are then clamped by the USER9 position worker below.
        n = {a[0], a[1], a[6] & 0xffff, 0, a[6] & 0xffff, short(a[2]), short(a[3]), short(a[4]), short(a[5]), {}, {}, {}, false, false};
        User::ConstrainShellPosition(n.hwnd, n.after, n.flags);
        if (!candidate && !(n.flags & 0x10)) candidate = n.hwnd; // USER
        n.block = Allocate(PositionBlockBytes);
        if (!n.block) { last_error = 8; for (unsigned k = 0; k < i; ++k) if (entries[k].block) Free(entries[k].block); return 0; }
        if (!(n.flags & 0x400))
        {
            unsigned pos[7] = {n.hwnd, n.after, (unsigned)n.x, (unsigned)n.y, (unsigned)n.width, (unsigned)n.height, n.flags};
            Store(n.block, pos, sizeof pos);
            PositionSend(n.hwnd, 0x46, 0, n.block, n.block);
            if (state == Failed || state == Exited) break;
            if (!User::IsWindow(n.hwnd)) { n.dropped = true; continue; }
            Read(n.block, pos, sizeof pos);
            // The return thunk also packs a Win32 callback's edits back into
            // the WORD WINDOWPOS before the 16-bit position worker resumes.
            n.after = pos[1]; n.x = short(pos[2]); n.y = short(pos[3]); n.width = short(pos[4]); n.height = short(pos[5]);
            n.flags = pos[6] & 0xffff;
        }
        Gdi::RECT r = {};
        User::GetWindowRect(n.hwnd, &r);
        n.old_window = r;
        Gdi::POINT p = {r.left, r.top};
        if (const unsigned parent = User::Parent(n.hwnd)) User::ScreenToClient(parent, &p);
        Gdi::RECT client = {};
        User::GetClientRect(n.hwnd, &client);
        Gdi::POINT origin = {0, 0};
        User::ScreenToClient(n.hwnd, &origin);
        n.old_client = {-origin.x, -origin.y, -origin.x + client.right, -origin.y + client.bottom};
        // Complete the request.
        n.flags |= 0x1800;
        if (n.flags & 2) { n.x = p.x; n.y = p.y; }
        else if (n.x == p.x && n.y == p.y) n.flags |= 2;
        if (n.flags & 1) { n.width = r.right - r.left; n.height = r.bottom - r.top; }
        else
        {
            if (n.width < 0) n.width = 0;
            if (n.height < 0) n.height = 0;
            if (n.width == r.right - r.left && n.height == r.bottom - r.top) n.flags |= 1;
        }
        // an invisible window cannot hide and does not redraw
        // unless it is being shown; a visible one cannot be shown again.
        if (!(User::Style(n.hwnd) & 0x10000000))
        {
            n.flags &= ~0x80u;
            if (!(n.flags & 0x40)) n.flags |= 8;
        }
        else n.flags &= ~0x40u;
        // the insertion check marks an unchanged Z-order.
        if (!(n.flags & 4) && User::OrderUnchanged(n.hwnd, n.after)) n.flags |= 4;
        // nothing to draw when nothing changes or an ancestor hides it.
        const unsigned parent = User::Parent(n.hwnd);
        const bool will_show = (n.flags & 0x40) && (!parent || User::IsVisible(parent));
        if (!(n.flags & 8) && ((n.flags & 0x18e7) == 0x1807 || (!User::IsVisible(n.hwnd) && !will_show))) n.flags |= 8;
        if (!(n.flags & 1) || (n.flags & 0x20))
        {
            // A frame change updates WINDOWEDGE before the default
            // WM_NCCALCSIZE handler uses it. Keep the old client geometry
            // available to the callback until it returns its new rectangle.
            if (n.flags & 0x20)
            {
                unsigned extended = User::ExtendedStyle(n.hwnd);
                if (Win98Nonclient::HasWindowEdge(User::Style(n.hwnd), extended, User::ModernWindow(n.hwnd)))
                    extended |= 0x100;
                else
                    extended &= ~0x100u;
                User::SetStyleWords(n.hwnd, extended, true);
            }
            // NCCALCSIZE_PARAMS: the proposed window rectangle, the old
            // window rectangle, the old client rectangle, then the WINDOWPOS.
            Gdi::RECT proposed = {n.x, n.y, n.x + n.width, n.y + n.height};
            if (const unsigned parent = User::Parent(n.hwnd))
            {
                Gdi::POINT o = {0, 0};
                User::ScreenToClient(parent, &o);
                proposed = {proposed.left - o.x, proposed.top - o.y, proposed.right - o.x, proposed.bottom - o.y};
            }
            Gdi::RECT rects[3] = {proposed, n.old_window, n.old_client};
            unsigned pos[7] = {n.hwnd, n.after, (unsigned)n.x, (unsigned)n.y, (unsigned)n.width, (unsigned)n.height, n.flags};
            Store(n.block, pos, sizeof pos);
            Store(n.block + 28, rects, sizeof rects);
            const unsigned pointer = n.block;
            Store(n.block + 28 + 48, &pointer, 4);
            PositionSend(n.hwnd, 0x83, 1, n.block + 28, n.block);
            if (state == Failed || state == Exited) break;
            if (!User::IsWindow(n.hwnd)) { n.dropped = true; continue; }
            n.calculated = Read(n.block + 28, &n.new_client, sizeof n.new_client);
        }
    }
    unsigned result = 1;
    if (state != Failed && state != Exited)
    {
        // geometry, order and visibility of every entry.
        for (unsigned i = 0; i < count; ++i)
        {
            auto &n = entries[i];
            if (n.dropped || !User::IsWindow(n.hwnd)) continue;
            if ((!(n.flags & 2) || !(n.flags & 1)) && !User::MoveWindow(n.hwnd, n.x, n.y, n.width, n.height, true)) { result = 0; break; }
            // Earlier entries in this deferred batch may already have put
            // this window at its requested position in the sibling order.
            // Do not report a second Z-order change for that same move.
            if (!(n.flags & 4) && User::OrderUnchanged(n.hwnd, n.after)) n.flags |= 4;
            if (!(n.flags & 4) && !User::OrderWindow(n.hwnd, n.after, false)) { result = 0; break; }
            if (n.flags & 64) User::ShowWindow(n.hwnd, 8, true);
            if (n.flags & 128) User::ShowWindow(n.hwnd, 0, true);
            // USER applies the pending SetWindowRgn entry after the
            // changing/calculation callbacks, before paint and changed.
            if(n.hwnd==region_window)User::ApplyWindowRegion(n.hwnd,region);
            // SWP_FRAMECHANGED applies a style change that WM_NCCALCSIZE alone
            // does not repaint: a window shown this way (SetWindowLong(GWL_STYLE,
            //..|WS_VISIBLE) followed by SetWindowPos(SWP_FRAMECHANGED), the
            // idiom Delphi/VB use instead of ShowWindow) never gets a WM_PAINT
            // otherwise. With unchanged geometry only the frame is invalid;
            // a caller that also asked for SWP_NOREDRAW keeps that request.
            // SWP_FRAMECHANGED is also the moment the frame follows a
            // style word that SetWindowLong wrote without touching geometry
            // (USER writes the words alone). MoveWindow above already
            // lays the frame out again; with SWP_NOMOVE|SWP_NOSIZE it does not
            // run, and without this call a window that changed its border,
            // caption or child bits would keep the old client rectangle.
            if (n.flags & 0x20) User::RelayoutFrame(n.hwnd);
            if (n.calculated) User::ApplyClientRect(n.hwnd, n.new_client);
            Gdi::RECT client = {};
            User::GetClientRect(n.hwnd, &client);
            Gdi::POINT origin = {0, 0};
            User::ScreenToClient(n.hwnd, &origin);
            // Windows 98 sets SWP_NOCLIENTSIZE and SWP_NOCLIENTMOVE first and
            // takes each away only when the client rectangle after
            // WM_NCCALCSIZE differs; SWP_FRAMECHANGED alone clears neither.
            // Unreal Tournament answers WM_SIZE with SetMenu(NULL), whose
            // frame change must not bring another WM_SIZE.
            if (-origin.x != n.old_client.left || -origin.y != n.old_client.top) n.flags &= ~0x1000u;
            if (client.right != n.old_client.right - n.old_client.left || client.bottom != n.old_client.bottom - n.old_client.top) n.flags &= ~0x800u;
            if ((n.flags & 0x20) && !(n.flags & 8))
            {
                // A recalculated frame with unchanged geometry does not make
                // the client or its children invalid (native frame_paint).
                if ((n.flags & 0x1803u) == 0x1803u) User::InvalidateFrame(n.hwnd);
                else
                {
                    User::InvalidateFrame(n.hwnd);
                    User::InvalidateRect(n.hwnd, nullptr, true);
                }
            }
            if (!(n.flags & 9u)) User::InvalidateFrame(n.hwnd);
        }
        // USER copies valid pixels, restores SPBs and invalidates
        // exposed regions before activation and WINDOWPOSCHANGED callbacks.
        User::FinishWindowPositions();
        // Send EVENT_OBJECT_SHOW, _HIDE and
        // _LOCATIONCHANGE, attributed to the window's queue. The hooks run
        // once the geometry of every entry stands.
        if (result && User::WinEventListening())
            for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
            {
                auto &n = entries[i];
                if (n.dropped || !User::IsWindow(n.hwnd)) continue;
                if (n.flags & 64) WinEventNotify(0x8002, n.hwnd, 0, 0, true);
                if ((n.flags & 128) && User::IsWindow(n.hwnd)) WinEventNotify(0x8003, n.hwnd, 0, 0, true);
                if ((!(n.flags & 2) || !(n.flags & 1)) && User::IsWindow(n.hwnd)) WinEventNotify(0x800b, n.hwnd, 0, 0, true);
            }
        // the activation candidate.
        if (result && candidate && User::IsWindow(candidate))
        {
            using namespace Win98WindowParent;
            const unsigned kind = ActivationKind(User::Style(candidate), candidate, User::ThreadActivation(User::WindowThread(candidate)).active, true);
            if (kind == 1) PositionSend(candidate, 0x22, 0, 0);
            else if (kind == 2) ActivateWindow(candidate);
        }
        if (result) User::RefreshShellActivation();
        // USER erases the affected sibling list's parent before
        // WINDOWPOSCHANGED. A pending parent erase must survive neither a
        // child's show nor a move that exposes part of its client area.
        for (unsigned i = 0; result && i < count && state != Failed && state != Exited; ++i)
        {
            const auto &n = entries[i];
            if (n.dropped || (n.flags & 0x2008u) || (n.flags & 0x18e7u) == 0x1807u) continue;
            const unsigned parent = User::Parent(n.hwnd);
            if (parent && User::IsWindow(parent) && User::WindowThread(parent) == User::CurrentThread())
                RedrawImmediate(parent, 2, 4);
            else if (!parent && n.hwnd != region_window && (n.flags & 0x1803u) != 0x1803u &&
                     User::WindowThread(n.hwnd) == User::CurrentThread())
                // The desktop-parent erase walk includes the changed
                // top-level window. SetWindowRgn has its frame stage below.
                RedrawImmediate(n.hwnd, 2, 4);
        }
        // The newly shown window is erased before WINDOWPOSCHANGED. This
        // includes frameless popups: their first NCPAINT precedes ERASEBKGND
        // too (native show_paint). Children created by the later WM_SIZE must
        // not be pulled into this earlier erase walk. DEFERERASE suppresses it.
        for (unsigned i = 0; result && i < count && state != Failed && state != Exited; ++i)
        {
            auto &n = entries[i];
            if (!n.dropped && (n.flags & 0x40) && !(n.flags & 0x2008) && User::IsWindow(n.hwnd) &&
                User::WindowThread(n.hwnd) == User::CurrentThread())
                RedrawImmediate(n.hwnd, 2, 4);
        }
        // WM_WINDOWPOSCHANGED for everything that changed.
        for (unsigned i = 0; result && i < count && state != Failed && state != Exited; ++i)
        {
            auto &n = entries[i];
            if (n.dropped || !User::IsWindow(n.hwnd) || (n.flags & 0x18e7) == 0x1807) continue;
            // An iconic window has no client update rectangle, but its
            // changed frame is still painted before WINDOWPOSCHANGED.
            if ((n.flags & 0xa008u) == 0x8000u &&
                (User::Style(n.hwnd) & 0x30000000u) == 0x30000000u && User::TakeFramePaint(n.hwnd))
            {
                PositionSend(n.hwnd, 0x85, 1, 0);
                if (!User::IsWindow(n.hwnd) || state == Failed || state == Exited) continue;
            }
            if ((n.flags & 0x2028u) == 0x20u && (n.flags & 0x1803u) == 0x1803u &&
                memcmp(&n.old_window, &n.old_client, sizeof n.old_window) && n.hwnd != region_window &&
                !User::AncestorNeedsPaint(n.hwnd))
            {
                // The unchanged frame is erased before WINDOWPOSCHANGED,
                // including an empty client clip. DEFERERASE leaves only
                // the frame pending for the later BeginPaint instead.
                if ((n.flags & 0x1803u) == 0x1803u && User::TakeFramePaint(n.hwnd))
                {
                    PositionSend(n.hwnd, 0x85, 1, 0);
                    if (!User::IsWindow(n.hwnd) || state == Failed || state == Exited) continue;
                    const unsigned token = User::EraseToken(n.hwnd);
                    if (const auto dc = User::GetUpdateDC(n.hwnd))
                    {
                        const bool handled = PositionSend(n.hwnd, 0x14, dc, 0) != 0;
                        User::RememberErase(n.hwnd, handled);
                        if (handled || !EraseAgain()) User::Erased(n.hwnd, token);
                        User::ReleaseDC(n.hwnd, dc);
                    }
                }
                RedrawImmediate(n.hwnd, 2, 4);
                if (!User::IsWindow(n.hwnd) || state == Failed || state == Exited) continue;
            }
            if(n.hwnd==region_window&&!(n.flags&8)){
                User::TakeFramePaint(n.hwnd);
                PositionSend(n.hwnd,0x85,1,0);
                RedrawImmediate(n.hwnd,2,4);
                if(!User::IsWindow(n.hwnd)||state==Failed||state==Exited)continue;
            }
            unsigned pos[7] = {n.hwnd, n.after, (unsigned)n.x, (unsigned)n.y, (unsigned)n.width, (unsigned)n.height, n.flags};
            Store(n.block, pos, sizeof pos);
            size_reported = 0;
            PositionSend(n.hwnd, 0x47, 0, n.block, n.block);
            // Windows served by native procedures that never reach
            // DefWindowProc (common controls, dialogs) still learn their size.
            const unsigned procedure = User::Procedure(n.hwnd);
            const bool native = !procedure || (procedure & 0xffff0000u) == 0x71000000u;
            if (User::IsWindow(n.hwnd) && native && size_reported != n.hwnd && !(n.flags & 0x800))
            {
                Gdi::RECT client = {};
                User::GetClientRect(n.hwnd, &client);
                PositionSend(n.hwnd, 5, 0, (client.bottom & 65535) << 16 | (client.right & 65535));
            }
        }
    }
    for (unsigned i = 0; i < count; ++i) if (entries[i].block) Free(entries[i].block);
    return state == Failed ? 0 : result;
}
unsigned Application::PositionApi(ApiName name, const unsigned *a)
{
    if (API_IS(name,"SetWindowPos"))
    {
        unsigned position[1][7];
        memcpy(position[0], a, sizeof position);
        return PositionWindows(1, position);
    }
    if (API_IS(name,"BeginDeferWindowPos"))
    {
        // USER uses a signed Win16 count; requests <=4 use the
        // minimum allocation, including negative hints.
        if (static_cast<short>(a[0]) > 64)
        {
            last_error = 8;
            return 0;
        }
        for (unsigned i = 0; i < 8; ++i)
            if (!positions[i].used)
            {
                auto &batch = positions[i];
                batch.used = true;
                batch.count = 0;
                return 0x20000000 | (batch.generation << 8) | (i + 1);
            }
        last_error = 8;
        return 0;
    }
    unsigned i = (a[0] & 255) - 1;
    if ((a[0] & 0xf0000000) != 0x20000000 || i >= 8 || !positions[i].used ||
        positions[i].generation != ((a[0] >> 8) & 0xfffff))
    {
        // Win98's USER handle validation leaves Win32 LastError untouched.
        return 0;
    }
    auto &batch = positions[i];
    auto release = [&]()
    {
        batch.used = false;
        batch.count = 0;
        batch.generation = batch.generation % 0xfffff + 1;
    };
    if (API_IS(name,"EndDeferWindowPos"))
    {
        unsigned saved[64][7], count = 0;
        // Windows can disappear after queuing and before EndDeferWindowPos.
        // The original skips those records and still applies surviving ones.
        for(unsigned n=0;n<batch.count;++n)
            if(User::IsWindow(batch.args[n][0]))memcpy(saved[count++],batch.args[n],sizeof saved[0]);
        release();
        return PositionWindows(count, saved);
    }
    if (!User::IsWindow(a[1]))
    {
        release();
        return 0;
    }
    // An invalid insertion window rejects this call without consuming the
    // batch. SWP_NOZORDER ignores the insertion window entirely.
    if(!(a[7]&4)&&a[2]>1&&a[2]!=~0u&&a[2]!=~1u&&!User::IsWindow(a[2]))return 0;
    // USER always appends a record. In particular, a later NOSIZE
    // request for the same window uses its pre-batch dimensions, not the
    // size of an earlier queued request.
    if (batch.count == 64)
    {
        last_error = 8;
        release();
        return 0;
    }
    memcpy(batch.args[batch.count++], a + 1, 7 * 4);
    return a[0];
}
} // namespace X86
