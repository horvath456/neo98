// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_rect.h"
namespace Win98Paint
{
struct Paint16
{
    unsigned short fields[8];
    unsigned char reserved[16];
};
struct Paint32
{
    unsigned dc, erase;
    Win98::Rect rect;
    unsigned restore, incremental;
    unsigned char reserved[32];
};
static_assert(sizeof(Paint16) == 32 && sizeof(Paint32) == 64, "Win98 paint wire layouts");
inline int Signed(unsigned short value) { return value & 0x8000 ? int(value) - 65536 : int(value); }
// USER32. Only the first 48 bytes of the output change.
inline void Expand(const Paint16 &in, Paint32 &out)
{
    out.dc = in.fields[0];
    out.erase = Signed(in.fields[1]);
    out.rect = {Signed(in.fields[2]), Signed(in.fields[3]), Signed(in.fields[4]), Signed(in.fields[5])};
    out.restore = Signed(in.fields[6]);
    out.incremental = Signed(in.fields[7]);
    for (unsigned i = 0; i < 16; ++i)
        out.reserved[i] = in.reserved[i];
}
// USER32: truncate eight DWORDs and copy sixteen bytes.
inline void Narrow(const Paint32 &in, Paint16 &out)
{
    const unsigned fields[] = {in.dc,
                               in.erase,
                               unsigned(in.rect.left),
                               unsigned(in.rect.top),
                               unsigned(in.rect.right),
                               unsigned(in.rect.bottom),
                               in.restore,
                               in.incremental};
    for (unsigned i = 0; i < 8; ++i)
        out.fields[i] = fields[i];
    for (unsigned i = 0; i < 16; ++i)
        out.reserved[i] = in.reserved[i];
}
inline void NativeOutput(const Paint32 &in, Paint32 &out)
{
    Paint16 packed;
    Narrow(in, packed);
    Expand(packed, out);
    // Native GDI generation handles replace the original 16-bit handle table.
    out.dc = in.dc;
}
} // namespace Win98Paint
