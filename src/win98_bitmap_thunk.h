// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 GDI32 generated bitmap thunks.
#pragma once
namespace Win98BitmapThunk
{
using U = unsigned;
static_assert(sizeof(U) == 4, "Win98 bitmap thunks require 32-bit unsigned");
struct Bitmap16
{
    U type, width, height, stride, planes, bpp, bits;
};
// Providers: Read16/Read32(flatAddress); MapBits(flat,bytes,mapped,cookie)
// returning success and defining both outputs even on failure; UnmapBits(cookie);
// CreateBitmap16(Bitmap16); GetBitmapBits16/SetBitmapBits16(handle,count,mapped);
// Error(code). Native tokens may replace FAR pointers. Get/Set providers return
// the logical original DX:AX value; Create's return is narrowed to AX here.
// Guest pointer validation and the mapping allocator belong to the provider.

template <class E> U Failure(E &e) { e.Error(87); return 0; }
inline bool Multiply(U a, U b, U &result)
{
    if (b && a > 0xffffffffu / b) return false;
    result = a * b;
    return true;
}

template <class E> struct GuestBitmap
{
    E &environment;
    U pointer;
    U Read16(U offset) { return environment.Read16(pointer + offset); }
    U Read32(U offset) { return environment.Read32(pointer + offset); }
};
struct ArgumentBitmap
{
    U width, height, planes, bpp, bits;
    U Read16(U offset) const { return (offset == 16 ? planes : bpp) & 0xffffu; }
    U Read32(U offset) const
    { return offset == 4 ? width : offset == 8 ? height : offset == 20 ? bits : 0; }
};

// Reads follow the original order,
// including re-reading fields after the staged unsigned overflow checks.
template <class E, class B> U Convert(E &e, B &source)
{
    U row = source.Read16(18);
    const U width = source.Read32(4);
    if (!Multiply(row, width, row)) return Failure(e);
    if (row > 0xfffffff0u) return Failure(e);
    const U stride = ((row + 15) >> 4) << 1;
    U bytes;
    const U height = source.Read32(8);
    if (!Multiply(stride, height, bytes)) return Failure(e);
    const U planes = source.Read16(16);
    if (!Multiply(bytes, planes, bytes)) return Failure(e);
    Bitmap16 bitmap;
    bitmap.type = source.Read32(0) & 0xffffu;
    bitmap.width = source.Read32(4) & 0xffffu;
    bitmap.height = source.Read32(8) & 0xffffu;
    bitmap.stride = stride & 0xffffu; // The supplied bmWidthBytes is skipped.
    bitmap.planes = source.Read16(16) & 0xffu;
    bitmap.bpp = source.Read16(18) & 0xffu;
    const U bits = source.Read32(20);
    U cookie = 0;
    if (!e.MapBits(bits, bytes, bitmap.bits, cookie)) return Failure(e);
    const U result = e.CreateBitmap16(bitmap) & 0xffffu;
    e.UnmapBits(cookie);
    return result ? result : Failure(e);
}

// GDI32 -> GDI. The original builds a temporary BITMAP32
// and maps its stack address through KERNEL.357; this native reader replaces
// that address conversion and retains the WORD planes/bpp truncation.
template <class E> U Create(E &e, U width, U height, U planes, U bpp, U bits)
{
    ArgumentBitmap source{width, height, planes, bpp, bits};
    return Convert(e, source);
}
// GDI32 -> GDI. There is no inline null-pointer check.
template <class E> U Indirect(E &e, U bitmap32)
{
    GuestBitmap<E> source{e, bitmap32};
    return Convert(e, source);
}

// GDI32/ -> GDI. Unlike Create, the original
// ignores mapping carry and still invokes GDI with the mapper's returned bits.
template <class E> U Transfer(E &e, U handle, U count, U bits, bool write)
{
    if (!bits || !count) return Failure(e);
    U mapped = 0, cookie = 0;
    e.MapBits(bits, count, mapped, cookie);
    const U result = write ? e.SetBitmapBits16(handle & 0xffffu, count, mapped)
                           : e.GetBitmapBits16(handle & 0xffffu, count, mapped);
    e.UnmapBits(cookie);
    return result ? result : Failure(e);
}
template <class E> U GetBits(E &e, U handle, U count, U bits)
{ return Transfer(e, handle, count, bits, false); }
template <class E> U SetBits(E &e, U handle, U count, U bits)
{ return Transfer(e, handle, count, bits, true); }
} // namespace Win98BitmapThunk
