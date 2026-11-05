// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER HWND_BROADCAST: the recipient list and the per-window decisions.
// USER builds a snapshot and walks it.
// with flags 2 collects the desktop's children and their siblings only,
// so a broadcast reaches top-level windows and never descends into children.
#pragma once
namespace Win98Broadcast
{
using U = unsigned;
// HWND_BROADCAST as a 32-bit caller may write it: 0FFFFh, or -1 which USER32
// narrows to the same 16-bit handle.
inline bool IsBroadcast(U hwnd) { return hwnd == 0xffffu || hwnd == 0xffffffffu; }
// USER delivery selector, the value the caller leaves at [bp+0xa].
enum Mode
{
    Timeout = 0,
    Send = 1,
    Post = 2,
    Notify = 3,
    Callback = 4,
    Query = 5 // BroadcastSystemMessage: synchronous, stop on QUERY_DENY.
};
// The fields Windows 98 reads out of every candidate WND record.
struct Window
{
    U handle = 0;      // Zero entries are skipped; the task filter clears them in place.
    U class_atom = 0;  // WND +0x24 then class +6.
    U queue = 0;       // WND +0x20.
    U owner = 0;       // WND +0x0c, only the posted path inspects it.
    bool palette_aware = false;   // WND +0x2e bit 0.
    bool excluded = false;        // WND +0x2e bit 6.
    bool accepts_ini_name = true; // WND +0x2f bit 1.
};
struct Request
{
    U message = 0;
    U wparam = 0;
    bool has_name = false; // A non-null WM_WININICHANGE lParam string.
    bool palette_source = false;
    U task = 0;              // Nonzero restricts the broadcast to one queue.
    U menu_atom = 0x8000;    // USER reserves these class atoms; 0x100a holds the third.
    U dropdown_atom = 0x8003;
    U reserved_atom = 0xffffffffu; // USER data 0x100a; no atom when the caller has none.
};
// Complete per-window decision sequence USER.
inline bool Accepts(const Request &r, const Window &w)
{
    if (!w.handle)
        return false;
    if (w.class_atom &&
        (w.class_atom == r.menu_atom || w.class_atom == r.dropdown_atom ||
         w.class_atom == r.reserved_atom))
        return false;
    if (r.palette_source && r.message == 0x311 && r.wparam && !w.palette_aware)
        return false;
    if (r.task && w.queue != r.task)
        return false;
    if (r.message >= 0x400 && r.message < 0xc000 && w.excluded)
        return false;
    if (r.message == 0x1a && r.has_name && !w.accepts_ini_name)
        return false;
    return true;
}
// USER: the posted variant skips a window that still owns another record.
inline bool Postable(const Window &w) { return w.owner == 0; }
} // namespace Win98Broadcast
