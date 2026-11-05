// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for the original-code comparisons of PostThreadMessage and
// RedrawWindow. Every provider records the same trace the Unicorn oracle
// records for the corresponding original call.
#include "../src/win98_post_thread_message.h"
#include "../src/win98_redraw_window.h"
using U = unsigned;

// ---------------------------------------------------------------- post ----
struct PostState {
    U queue_handle, queue_valid, post_result, allowed;
    U thunk_message, thunk_wparam, thunk_extra, thunk_lparam;
    U broadcast_result, record, record_owner, record_next, queue_six;
    U written_offset, written_value;
    U trace[96], count;
};
struct PostProvider {
    PostState &s;
    void Event(U a, U b = 0, U c = 0, U d = 0)
    {
        if (s.count + 4 <= 96) { s.trace[s.count++] = a; s.trace[s.count++] = b; s.trace[s.count++] = c; s.trace[s.count++] = d; }
    }
    // The table pair is read from the original and supplied here.
    U MessageAllowed(U) { return s.allowed; }
    void ThunkMessage(U kind, U target, U message, U high, U low, U lparam, Win98PostThreadMessage::Thunked &out)
    {
        Event(2, kind, message, (high << 16) | low);
        Event(3, target, lparam);
        out.message = s.thunk_message; out.wparam = s.thunk_wparam;
        out.extra = s.thunk_extra; out.lparam = s.thunk_lparam;
    }
    U ThreadQueue(U thread) { Event(4, thread); return s.queue_handle; }
    // Windows 98 answers the selector only for an object carrying 'HQ' at +56h.
    U ValidateQueue(U handle) { return s.queue_valid ? handle : 0; }
    void FlushPostedMessages() { Event(6); }
    U PostEvent(U queue, U offset, U message, U wparam, U lparam, U extra)
    {
        Event(7, queue, offset, message);
        Event(8, wparam, lparam, extra);
        return s.post_result;
    }
    U QueueWord(U queue, U offset) { return offset == 0x10 ? s.record : offset == 6 ? s.queue_six : 0; }
    U DataWord(U offset) { return offset == s.record + 0x0e ? s.record_owner : s.record_next; }
    void SetDataWord(U offset, U value) { s.written_offset = offset; s.written_value = value; }
    U Broadcast(U hwnd, U message, U wparam, U lparam, U extra)
    {
        Event(10, hwnd, message, wparam);
        Event(11, lparam, extra);
        return s.broadcast_result;
    }
};
extern "C" U post_thread_message_run(PostState *state, U broadcast, U target, U message, U wparam, U lparam)
{
    PostProvider e{*state};
    return Win98PostThreadMessage::Thunk(e, broadcast, target, message, wparam, lparam);
}
extern "C" U post_message_route(U hwnd) { return U(Win98PostThreadMessage::PostMessageRoute(hwnd)); }

// -------------------------------------------------------------- redraw ----
using Win98RedrawWindow::Rect;
using Win98RedrawWindow::Window;
enum { Windows = 8 };
struct RedrawState {
    Window w[Windows];
    U desktop, task, scratch[3], valid, types[Windows];
    U window_rect[Windows * 8], window_bound[Windows * 4];
    U classify, combine, clip, watching, region_box[4], task_flags[Windows];
    U error_object, error_code;
    U trace[512], count;
};
struct RedrawProvider {
    RedrawState &s;
    bool failed = false;
    void Event(U a, U b = 0, U c = 0, U d = 0)
    {
        if (s.count + 4 <= 512) { s.trace[s.count++] = a; s.trace[s.count++] = b; s.trace[s.count++] = c; s.trace[s.count++] = d; }
    }
    static U Handle(U p) { return p ? (p - 0x6000) / 128 : 0; }
    Window Read(U p) { const U h = Handle(p); return h < Windows ? s.w[h] : Window{}; }
    U Desktop() { return 0x6000 + s.desktop * 128; }
    U Scratch(U i) { return s.scratch[i]; }
    U CurrentTask() { Event(20); return s.task; }
    U Resolve(U hwnd) { Event(21, hwnd); return hwnd < Windows && (s.valid & (1u << hwnd)) ? 0x6000 + hwnd * 128 : 0; }
    U ObjectType(U h) { Event(22, h); return h < Windows ? s.types[h] : 0; }
    void Error(U object, U code) { Event(23, object, code); s.error_object = object; s.error_code = code; failed = true; }
    void WindowRect(U p, Rect &out, U kind)
    {
        const U h = Handle(p); Event(24, h, kind);
        const U *r = s.window_rect + (h * 2 + (kind ? 1 : 0)) * 4;
        out = Rect{int(short(r[0])), int(short(r[1])), int(short(r[2])), int(short(r[3]))};
    }
    Rect WindowBound(U p)
    {
        const U h = Handle(p); const U *r = s.window_bound + h * 4;
        return Rect{int(short(r[0])), int(short(r[1])), int(short(r[2])), int(short(r[3]))};
    }
    // A pure rectangle intersection; it takes no trace entry.
    bool Intersect(Rect &out, const Rect &a, const Rect &b)
    {
        const int l = a.left > b.left ? a.left : b.left, t = a.top > b.top ? a.top : b.top;
        const int r = a.right < b.right ? a.right : b.right, o = a.bottom < b.bottom ? a.bottom : b.bottom;
        if (l >= r || t >= o) { out = Rect{}; return false; }
        out = Rect{l, t, r, o}; return true;
    }
    U Classify(U hrgn, const Rect &r) { Event(25, hrgn, U(r.left) & 65535u, U(r.top) & 65535u); return s.classify; }
    void SetRectRgn(U h, const Rect &r)
    {
        Event(26, h, U(r.left) & 65535u, U(r.top) & 65535u);
        Event(27, U(r.right) & 65535u, U(r.bottom) & 65535u);
    }
    U Combine(U dst, U a, U b, U mode) { Event(28, dst, a, (b << 8) | mode); return s.combine; }
    void OffsetRgn(U h, U dx, U dy) { Event(29, h, dx, dy); }
    U TaskFlags(U queue) { Event(30, queue); return s.task_flags[queue & 7]; }
    void Apply(U p, U hrgn, U task, U flags) { Event(31, Handle(p), hrgn, ((flags & 65535u) << 16) | (task & 65535u)); }
    void Notify(U p, const Rect &r) { Event(32, Handle(p), U(r.left) & 65535u, U(r.top) & 65535u); }
    void WindowRegion(U p, U region, U client) { Event(33, Handle(p), region, client); }
    bool ClipToVisible(U p, Rect &r) { Event(34, Handle(p), U(r.left) & 65535u); return s.clip != 0; }
    U Watching() { return s.watching; }
    void RegionBox(U h, Rect &out)
    {
        Event(35, h);
        out = Rect{int(short(s.region_box[0])), int(short(s.region_box[1])),
                   int(short(s.region_box[2])), int(short(s.region_box[3]))};
    }
    void Watch(U p, const Rect &r) { Event(36, Handle(p), U(r.left) & 65535u, U(r.right) & 65535u); }
    void UpdateNow(U p, U recurse) { Event(37, Handle(p), recurse); }
    void EraseNow(U p, U mode) { Event(38, Handle(p), mode); }
};
extern "C" U redraw_window_run(RedrawState *state, U hwnd, const int *rect, U hrgn, U flags)
{
    RedrawProvider e{*state};
    Rect r{};
    if (rect) r = Rect{rect[0], rect[1], rect[2], rect[3]};
    const U result = Win98RedrawWindow::Entry(e, hwnd, rect ? &r : nullptr, hrgn, flags);
    return e.failed ? 0 : result;
}

extern "C" unsigned post_message_allowed(unsigned message)
{ return Win98PostThreadMessage::MessageAllowed(message); }
