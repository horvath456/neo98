// SPDX-License-Identifier: GPL-2.0-or-later
// GDI.EXE public Win16 Get/SetBitmapBits entry order.
#pragma once
namespace Win98BitmapEntry
{
using U = unsigned;
// BitmapType represents GDI's local-heap lookup and masked signature.
// ProbeReadByte/ProbeWriteByte adapt the original memory probes and GP boundary.
// Reject reports KERNEL.327's diagnostic; that routine unwinds to a zero return.
// GetBitmapBits/SetBitmapBits then enter the separately ported storage core.
// GDI; type check.
template<class E> U GetBits(E &e, U handle, U count, U bits)
{
    handle &= 0xffffu;
    if (e.BitmapType(handle) != 0x4f4b) { e.Reject(0x6066, handle); return 0; }
    if (!e.ProbeWriteByte(bits)) { e.Reject(0x7007, bits); return 0; }
    return e.GetBitmapBits(handle, count, bits);
}
template<class E> U SetBits(E &e, U handle, U count, U bits)
{
    handle &= 0xffffu;
    if (e.BitmapType(handle) != 0x4f4b) { e.Reject(0x6066, handle); return 0; }
    if (!e.ProbeReadByte(bits)) { e.Reject(0x7007, bits); return 0; }
    return e.SetBitmapBits(handle, count, bits);
}
} // namespace Win98BitmapEntry
