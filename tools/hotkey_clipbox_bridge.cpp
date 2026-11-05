// SPDX-License-Identifier: GPL-2.0-or-later
// Bridge for the original-code comparisons of the hot-key family and
// GetClipBox. Every provider records the same trace the Unicorn oracle records
// for the corresponding original call.
#include "../src/win98_hotkey.h"
#include "../src/win98_gdi_clip_box.h"
using U = unsigned;

// -------------------------------------------------------------- hot keys ---
// The list is a fixed table; a token is the one-based slot, which is what the
// original's near offset inside USER's local heap is.
enum { HotKeys = 16, HotTrace = 128 };
struct HotKeyRecord { U used, next, queue, id, window, modifiers, vk, scan; };
struct HotKeyState {
    HotKeyRecord record[HotKeys];
    U head, task, allocate_fails, keys[8], window_task[8], window_valid;
    U error_code, last_task;
    U trace[HotTrace], count;
};
struct HotKeyProvider {
    HotKeyState &s;
    void Event(U a, U b = 0, U c = 0, U d = 0)
    {
        if (s.count + 4 <= HotTrace) { s.trace[s.count++] = a; s.trace[s.count++] = b; s.trace[s.count++] = c; s.trace[s.count++] = d; }
    }
    HotKeyRecord &R(U t) { return s.record[t - 1]; }
    U Head() { return s.head; }
    void SetHead(U t) { s.head = t; }
    U Next(U t) { return R(t).next; }
    void SetNext(U t, U v) { R(t).next = v; }
    U Queue(U t) { return R(t).queue; }
    void SetQueue(U t, U v) { R(t).queue = v; }
    U Id(U t) { return R(t).id; }
    void SetId(U t, U v) { R(t).id = v; }
    U Window(U t) { return R(t).window; }
    void SetWindow(U t, U v) { R(t).window = v; }
    U Modifiers(U t) { return R(t).modifiers; }
    void SetModifiers(U t, U v) { R(t).modifiers = v; }
    U Vk(U t) { return R(t).vk; }
    void SetVk(U t, U v) { R(t).vk = v; }
    void SetScan(U t, U v) { R(t).scan = v; }
    // KERNEL.5 LocalAlloc(LMEM_ZEROINIT, 10h).
    U Allocate()
    {
        Event(1, Win98HotKey::AllocateFlags, Win98HotKey::RecordBytes);
        if (s.allocate_fails) return 0;
        for (U i = 0; i < HotKeys; ++i)
            if (!s.record[i].used) { s.record[i] = HotKeyRecord{}; s.record[i].used = 1; return i + 1; }
        return 0;
    }
    void Free(U t) { Event(2, t); R(t) = HotKeyRecord{}; }
    U CurrentTask() { Event(3); return s.task; }          // KERNEL ordinal 625
    U MapHandle(U hwnd) { Event(4, hwnd); return (hwnd < 8 && (s.window_valid & (1u << hwnd))) ? hwnd : 0; }
    U WindowHandle(U window) { return window; }            // pWnd+46h
    U WindowTask(U window) { return window < 8 ? s.window_task[window] : 0; }  // pWnd+20h
    U MapVirtualKey(U vk) { Event(5, vk, 0); return 0x100 | (vk & 0xff); }     // KEYBOARD.131
    void ListChanged() { Event(6, s.head); }               //
    void ParameterError(U code) { Event(7, code); s.error_code = code; }       // KERNEL ordinal 327
    bool KeyDown(U vk) { return (s.keys[vk >> 5] >> (vk & 31)) & 1u; }
    void PostThread(U queue, U message, U wparam, U lparam) { Event(8, queue, message, wparam); Event(9, lparam); }
    void PostWindow(U hwnd, U message, U wparam, U lparam) { Event(10, hwnd, message, wparam); Event(11, lparam); }
    void SetLastTask(U queue) { s.last_task = queue; }     // USER DGROUP 150h
};
extern "C" U hotkey_register(HotKeyState *state, U hwnd, U id, U modifiers, U vk)
{ HotKeyProvider e{*state}; return Win98HotKey::RegisterEntry(e, hwnd, id, modifiers, vk); }
extern "C" U hotkey_unregister(HotKeyState *state, U hwnd, U id)
{ HotKeyProvider e{*state}; return Win98HotKey::UnregisterEntry(e, hwnd, id); }
extern "C" U hotkey_deliver(HotKeyState *state, U vk, U modifiers)
{ HotKeyProvider e{*state}; return Win98HotKey::DoHotKeyStuff(e, vk, modifiers); }
extern "C" void hotkey_free_window(HotKeyState *state, U handle)
{ HotKeyProvider e{*state}; Win98HotKey::FreeWindow(e, handle); }
extern "C" void hotkey_free_task(HotKeyState *state)
{ HotKeyProvider e{*state}; Win98HotKey::FreeTask(e); }

// ------------------------------------------------------------- clip box ---
enum { ClipTrace = 64 };
struct ClipBoxState {
    U visible_invalid, metafile, meta_region, application_clip;
    U device_width, device_height, scratch, combine_result;
    int bounds[4];
    U region_bytes, region_bands, logical_shift;
    U error_code;
    U trace[ClipTrace], count;
};
struct ClipBoxProvider {
    ClipBoxState &s;
    void Event(U a, U b = 0, U c = 0, U d = 0)
    {
        if (s.count + 4 <= ClipTrace) { s.trace[s.count++] = a; s.trace[s.count++] = b; s.trace[s.count++] = c; s.trace[s.count++] = d; }
    }
    bool VisibleInvalid() { return s.visible_invalid != 0; }      // DC+0Eh bit 2
    void RealizeVisible() { Event(1); }                           //
    U MetafileObject() { return s.metafile; }                     // DC+106h
    U MetaRegion() { return s.meta_region; }                      // DC+14h
    U ApplicationClip() { return s.application_clip; }            // DC+12h
    int DeviceWidth() { return int(s.device_width); }             // GDIINFO+8
    int DeviceHeight() { return int(s.device_height); }           // GDIINFO+0Ah
    U CreateEmptyRegion() { Event(2); return s.scratch; }         //
    void CombineRegions(U dst, U a, U b) { Event(3, dst, a, b); } // RGN_AND
    void DeleteRegion(U r) { Event(4, r); }                       //
    U RealizedClip() { return 0x24; }                             // DC+24h
    void RegionBounds(U region, Win98GdiClipBox::Rect &out)
    {
        Event(5, region);
        out = {s.bounds[0], s.bounds[1], s.bounds[2], s.bounds[3]};
    }
    U RegionBytes(U) { return s.region_bytes; }                   // region+0Eh
    U RegionBands(U) { return s.region_bands; }                   // region+12h
    void DeviceToLogical(Win98GdiClipBox::Rect &r)                //
    {
        Event(6, U(r.left) & 65535u, U(r.top) & 65535u);
        const int shift = int(s.logical_shift);
        r = {r.left + shift, r.top + shift, r.right + shift, r.bottom + shift};
    }
    void ParameterError(U code) { Event(7, code); s.error_code = code; }
};
extern "C" U clip_box_run(ClipBoxState *state, U signature, U rectangle, int *out)
{
    ClipBoxProvider e{*state};
    Win98GdiClipBox::Rect box{};
    const U result = Win98GdiClipBox::Body(e, signature, rectangle != 0, box);
    out[0] = box.left; out[1] = box.top; out[2] = box.right; out[3] = box.bottom;
    return result;
}
extern "C" U clip_box_classify(U used, U bands) { return Win98GdiClipBox::Classify(used, bands); }
extern "C" U clip_box_gate(U signature) { return Win98GdiClipBox::DcGate(signature) ? 1u : 0u; }
