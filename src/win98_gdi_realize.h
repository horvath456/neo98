#include "win98_gdi_palette_tables.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_gdi_palette.h"
// Palette management for RC_PALETTE devices: foreground/background
// realization, animation and SYSPAL_STATIC/SYSPAL_NOSTATIC transitions.
// System entries contain RGB and flags: 0x01 reserved, 0x10 occupied and 0x20
// claimed by the foreground in this pass. Fixed endpoint colors use 0x30 and
// are never reassigned. Each logical entry tracks its last foreground slot,
// current slot and previous slot for UpdateColors.
namespace Win98GdiRealize {
using U = unsigned;
struct System {
    U size = 256, statics = 20, saved_statics = 20;
    U color[256] = {};           // COLORREF byte order 0x00bbggrr
    unsigned char flags[256] = {};
    U saved[20] = {};            // Fixed colors for SYSPAL_STATIC.
    unsigned short time = 0;     // PDEVICE+A0h
    U foreground = 0, last = 0;  // Palette handles.
};
// The twenty fixed system-palette colors.
inline const auto &StaticColors = Win98GdiPalette::DefaultColors;
// The default palette uses the first ten and last ten system-palette slots.
inline U StaticSlot(const System &s, U index)
{ return index < s.saved_statics / 2 ? index : s.size - s.saved_statics + index; }
inline void Reset(System &s, U size)
{
    s = System{};
    s.size = size > 256 ? 256 : size;
    for (U i = 0; i < 20; ++i) s.saved[i] = StaticColors[i];
    if (s.size < 20) { s.statics = s.saved_statics = 0; return; }
    for (U i = 0; i < 20; ++i) {
        const U slot = StaticSlot(s, i);
        s.color[slot] = StaticColors[i];
        s.flags[slot] = 0x30;
    }
}
// PC_EXPLICIT specifies its own slot; PC_RESERVED never finds a match.
// Otherwise choose the nearest occupied, unreserved slot, taking the first on
// ties. found means an exact match; PC_NOCOLLAPSE returns a nearest slot
// without reporting a match.
inline bool Match(const System &s, U color, U &index)
{
    const U tag = color >> 24;
    if (tag & 2) { index = color & 0xffff; return true; }
    if (tag & 1) { index = 0; return false; }
    U best = ~0u; index = 0;
    for (U i = 0; i < s.size; ++i) {
        if (!(s.flags[i] & 0x10) || (s.flags[i] & 1)) continue;
        const U c = s.color[i];
        const int r = int(c & 255) - int(color & 255), g = int((c >> 8) & 255) - int((color >> 8) & 255),
                  b = int((c >> 16) & 255) - int((color >> 16) & 255);
        const U d = U(r * r + g * g + b * b);
        if (!d) { index = i; best = 0; break; }
        if (d < best) { best = d; index = i; }
    }
    return best == 0 && !(tag & 4);
}
struct Result { U changed = 0, mapped = 0; };
// `program` lists changed system-palette slots for the driver SETPALETTE
// call. A version-4.0-or-later foreground program also frees slots owned by
// other palettes.
inline Result Realize(System &s, Win98GdiPalette::Palette &p, U mode, bool is_default, bool modern = true);
inline void Precompute(const System &s, Win98GdiPalette::Palette &p, bool modern)
{
    // Copy foreground slots into a system-palette snapshot.
    System copy = s;
    Realize(copy, p, 3, false, modern);
}
inline Result Realize(System &s, Win98GdiPalette::Palette &p, U mode, bool is_default, bool modern)
{
    Result r;
    if (mode != 0 && (mode == 1 || is_default)) return r;
    const U state = p.state;
    for (U i = 0; i < p.count; ++i) p.entries[i].mapping[2] = p.entries[i].mapping[1];
    const U half = s.statics / 2;
    if (mode >= 2) {
        if (mode != 3) ++s.time;
        const unsigned char keep = modern ? 0xce : 0xde;
        for (U i = half; i < s.size - half; ++i) s.flags[i] &= keep;
        if (state == 2) {
            // Reuse slots from the previous foreground realization.
            for (U i = 0; i < p.count; ++i) {
                auto &e = p.entries[i];
                const U slot = e.mapping[0] & 255;
                if (slot != e.mapping[1]) ++r.mapped;
                e.mapping[1] = (unsigned short)slot;
                if (!(s.flags[slot] & 0x20) && !(e.color & 0x02000000)) {
                    const bool same = (s.color[slot] & 0xffffff) == (e.color & 0xffffff) &&
                                      ((s.flags[slot] ^ (e.color >> 24)) & 1) == 0;
                    if (!same) { s.color[slot] = e.color & 0xffffff; s.flags[slot] = (unsigned char)(e.color >> 24); ++r.changed; }
                }
                s.flags[slot] |= 0x30;
            }
            p.state = 2;
            return r;
        }
    }
    else if (state == 2) {
        if (p.time == s.time) { p.state = 2; return r; }   //
    }
    else Precompute(s, p, modern);                          //
    p.time = s.time;
    // Prefer an exact color, then a free slot, then the nearest color.
    // Initially both occupied and foreground flags prevent allocation. After
    // exhausting the table, only foreground claims remain protected; finally
    // use nearest matches.
    U free = 0; bool searching = true; unsigned char busy = 0x30;
    for (U i = 0; i < p.count; ++i) {
        auto &e = p.entries[i];
        U slot = 0;
        if (Match(s, e.color, slot)) {
            if (!(e.color & 0x02000000)) s.flags[slot] |= 0x30;
        }
        else if (searching) {
            U at = free;
            for (;;) {
                while (at < s.size && (s.flags[at] & busy)) ++at;
                if (at < s.size) break;
                if (busy & 0x10) { busy = 0x20; at = 0; continue; }
                searching = false;
                break;
            }
            if (searching) {
                free = at; slot = at;
                s.color[at] = e.color & 0xffffff;
                s.flags[at] = (unsigned char)((e.color >> 24) | 0x30);
                ++r.changed;
            }
        }
        e.mapping[1] = (unsigned short)slot;
    }
    if (mode >= 2) {
        for (U i = 0; i < p.count; ++i) p.entries[i].mapping[0] = p.entries[i].mapping[1];
        r.mapped = p.count;
    } else {
        for (U i = 0; i < p.count; ++i)
            if (p.entries[i].mapping[1] != p.entries[i].mapping[2]) ++r.mapped;
    }
    p.state = 2;
    p.realized = true;
    return r;
}
// The default palette needs no realization. Non-foreground palettes realize
// in the background. The foreground palette normally realizes in the
// foreground, with separate handling if another palette has intervened.
inline Result RealizePalette(System &s, U handle, Win98GdiPalette::Palette &p, bool is_default, bool modern = true)
{
    if (handle == s.last) return {};
    const U previous = s.last;
    s.last = handle;
    U mode;
    if (is_default) mode = 1;
    else if (handle != s.foreground) mode = 0;
    else mode = p.state == 2 && previous ? 1 : 2;
    return Realize(s, p, mode, is_default, modern);
}
// SelectPalette without bForceBackground selects the foreground palette.
inline void Select(System &s, U handle, bool background)
{
    if (!background && handle != s.foreground) { s.foreground = handle; s.last = 0; }
}
// Release foreground slots and restore the default foreground palette.
inline void RealizeDefault(System &s, U default_handle)
{
    const U half = s.statics / 2;
    for (U i = half; i < s.size - half; ++i) s.flags[i] &= 0xdf;
    ++s.time;
    s.last = s.foreground = default_handle;
}
// Animate a reserved logical entry in the system palette only if its mapped
// slot is also reserved. Return the lowest and highest changed slots.
inline bool Animate(System &s, Win98GdiPalette::Palette &p, U start, U count, const U *input, U &low, U &high)
{
    low = 0xff; high = 0; bool any = false;
    if (p.stock || start >= p.count) return false;
    if (count > p.count - start) count = p.count - start;
    for (U i = 0; i < count; ++i) {
        auto &e = p.entries[start + i];
        if (!(e.color & 0x01000000)) continue;
        e.color = input[i];
        const U slot = e.mapping[1] & 255;
        if (!p.realized || slot >= s.size || !(s.flags[slot] & 1)) continue;
        s.color[slot] = input[i] & 0xffffff;
        if (slot < low) low = slot;
        if (slot > high) high = slot;
        any = true;
    }
    return any && low <= high;
}
// SYSPAL_NOSTATIC releases all but black and white. SYSPAL_STATIC restores
// fixed colors. Return the previous setting.
inline U SetUse(System &s, U use, U &current)
{
    if (use != 1 && use != 2) use = 1;
    if (use == 2) {
        for (U i = 1; i + 1 < s.size; ++i) s.flags[i] = 0;
        s.statics = 2;
    } else {
        for (U i = 0; i < s.saved_statics; ++i) {
            const U slot = StaticSlot(s, i);
            s.color[slot] = s.saved[i];
            s.flags[slot] = 0x30;
        }
        s.statics = s.saved_statics;
    }
    const U previous = current;
    current = use;
    ++s.time;
    return previous;
}
// UpdateColors maps each previously realized DC palette slot to its current
// slot.
inline void UpdateTable(const Win98GdiPalette::Palette &p, U size, unsigned char *table)
{
    for (U i = 0; i < size; ++i) table[i] = (unsigned char)i;
    for (U i = 0; i < p.count; ++i) {
        const U from = p.entries[i].mapping[2];
        if (from < size) table[from] = (unsigned char)p.entries[i].mapping[1];
    }
}
}
