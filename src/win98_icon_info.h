// SPDX-License-Identifier: GPL-2.0-or-later
// Icon and cursor metadata, bitmap extraction, and hotspot behavior.
#pragma once
namespace Win98IconInfo
{
// USER. The single-bitmap form is planes * bits == 1.
inline bool Monochrome(unsigned planes, unsigned bits)
{
    return planes * bits == 1;
}
// USER. The mask covers the AND and the XOR half in that form.
inline unsigned MaskHeight(unsigned height, bool monochrome)
{
    return monochrome ? height * 2 : height;
}
// USER. Bit 0 of the resolved handle marks a cursor.
inline unsigned IsIcon(unsigned handle)
{
    return (handle & 1) ? 0u : 1u;
}
// USER32. The first three fields widen with sign, the two
// handles without, and the whole conversion is skipped for a null output.
inline unsigned SignExtend(unsigned word)
{
    return (word & 0x8000u) ? (word | 0xffff0000u) : word;
}
// The 16-bit record the worker fills, in its own order.
struct Info
{
    unsigned icon = 0, hotspot_x = 0, hotspot_y = 0, mask = 0, color = 0;
};
} // namespace Win98IconInfo
