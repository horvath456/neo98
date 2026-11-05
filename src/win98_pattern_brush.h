// SPDX-License-Identifier: GPL-2.0-or-later
// Original GDI BS_PATTERN construction and DIBENG monochrome realization.
#pragma once
namespace Win98PatternBrush
{
using U = unsigned;
// Providers share the bitmap port's Object, Read/Write8/16/32, Read/WritePointer,
// LocalAllocate/Free and MarkLocal. Additional providers: LockBitmap,
// UnlockBitmap, CreateBitmap(descriptor), SetBitmapBits, DriverCopyBitmap,
// BitmapDeviceMatchesScreen, CreateScreenBitmap, DibSectionPattern.
// Public additionally uses HeapFloor, InvalidBitmap and CallerModule.
// LocalFree releases ONLY the local object, retaining any global DDB storage.

// GDI. The caller of Create ignores this function's copy count.
template<class E> U CopyBitmap(E &e, U destination, U source)
{
    const U block = e.LockBitmap(source);
    const U object = e.Object(source);
    U result;
    if (e.Read16(object + 0x1e) & 6)
    {
        const U output = e.LockBitmap(destination);
        result = e.DriverCopyBitmap(e.Read16(object + 0x1c), output, block);
        e.UnlockBitmap(destination);
    }
    else
    {
        // LOOP with initial CX=0 performs 65,536 additions in the original.
        const U planes = e.Read8(block + 8);
        const U count = e.Read32(block + 0x0e) * (planes ? planes : 0x10000u);
        result = e.SetBitmapBits(destination, count, e.ReadPointer(block + 0x0a));
    }
    e.UnlockBitmap(source);
    return result;
}

// The BS_PATTERN specialization of GDI -> 091B..0B79, including
// the original local-object initializer at GDI.
template<class E> U Create(E &e, U &serial, U bitmap)
{
    bitmap &= 0xffffu;
    const U brush = e.LocalAllocate(2, 0x18);
    if (!brush) return 0;
    U object = e.Object(brush);
    e.Write16(object, 0);
    e.Write16(object + 2, 0x4f48);
    e.Write32(object + 4, serial++);
    e.Write32(object + 8, 0);
    e.Write16(object + 0x0c, 0);
    e.Write16(object + 0x0e, 3);
    e.Write32(object + 0x10, 0);
    e.Write16(object + 0x14, bitmap);
    e.MarkLocal(brush, 0x10);
    if (!bitmap) { e.LocalFree(brush); return 0; }
    const U input = e.Object(bitmap);
    if ((e.Read16(input + 2) & 0x5fffu) != 0x4f4b)
    { e.LocalFree(brush); return 0; }
    const U flags = e.Read16(input + 0x1e);
    // DIBSECTION conversion creates a BS_DIBPATTERN object, not a DDB snapshot.
    if (flags & 4)
    {
        if (e.DibSectionPattern(brush, input)) return brush;
        e.LocalFree(brush); return 0;
    }
    if ((flags & 1) || e.Read16(input + 0x14))
    { e.LocalFree(brush); return 0; }
    const U descriptor = e.LockBitmap(bitmap);
    U copy;
    if ((e.Read16(input + 0x1e) & 2) && e.BitmapDeviceMatchesScreen(input))
        copy = e.CreateScreenBitmap(e.Read16(descriptor + 2), e.Read16(descriptor + 4));
    else
    {
        const U bits = e.ReadPointer(descriptor + 0x0a);
        e.WritePointer(descriptor + 0x0a, 0);
        copy = e.CreateBitmap(descriptor);
        e.WritePointer(descriptor + 0x0a, bits);
    }
    if (!copy)
    { e.UnlockBitmap(bitmap); e.LocalFree(brush); return 0; }
    CopyBitmap(e, copy, bitmap); // Original preserves copy's handle over the call.
    const U storage = e.Read16(e.Object(copy) + 0x0e);
    e.LocalFree(copy);
    e.UnlockBitmap(bitmap);
    object = e.Object(brush);
    e.Write16(object + 0x14, storage);
    e.Write16(object + 0x16, bitmap);
    return brush;
}

// GDI and 0720..072E, used by the public wrapper.
template<class E> bool ValidBitmap(E &e, U bitmap)
{
    bitmap &= 0xffffu;
    if (!bitmap || bitmap <= e.HeapFloor()) return false;
    return (e.Read16(e.Object(bitmap) + 2) & 0x5fffu) == 0x4f4b;
}
// GDI32 narrows both handle arguments/results to WORD and
// contains no SetLastError call. KERNEL.327 parameter-error handling is supplied.
template<class E> U PublicCreate(E &e, U &serial, U bitmap)
{
    bitmap &= 0xffffu;
    if (!ValidBitmap(e, bitmap)) { e.InvalidBitmap(bitmap); return 0; }
    const U brush = Create(e, serial, bitmap) & 0xffffu;
    if (brush) e.Write16(e.Object(brush) + 0x0c, e.CallerModule());
    return brush;
}

// GDI -> 7F99/5C42 -> 7EA6, for local brush objects whose
// realization chain is empty. The guard is a native scope boundary; the full
// original dispatcher also destroys linked device realizations.
template<class E> bool UnrealizedSoftwareDestroy(E &e, U brush)
{
    const U object = e.Object(brush);
    if (e.Read16(object)) return false;
    const U style = e.Read16(object + 0x0e);
    if (style == 3 || style == 5) e.GlobalFree(e.Read16(object + 0x14));
    e.LocalFree(brush);
    return true;
}

// DIBENG supplies text in EDX and background in EAX. The 32-bpp
// expansion uses the shifted mask's carry to select each color.
inline U MonoColor(U mask_bit, U text, U background)
{ return ((0u - (mask_bit & 1u)) & (background ^ text)) ^ text; }

// DIBENG: rotate an eight-byte monochrome mask in place.
template<class E> void RotateMono8(E &e, U mask, U x, U y)
{
    U rows[8]; x &= 7; y &= 7;
    for (U i = 0; i < 8; ++i)
    {
        const U value = e.Read8(mask + i);
        rows[(i + y) & 7] = ((value >> x) | (value << ((8 - x) & 7))) & 255;
    }
    for (U i = 0; i < 8; ++i) e.Write8(mask + i, rows[i]);
}
// DIBENG: expand backwards, preserving the overlapping source.
template<class E> void ExpandMono8(E &e, U mask)
{
    for (U i = 8; i; --i)
    {
        const U value = e.Read8(mask + i - 1);
        e.Write32(mask + (i - 1) * 4, value * 0x01010101u);
    }
}
// DIBENG for an already validated ordinary one-bit DDB.
// The realized header at brush is owned by the caller; +0Eh has 32 mask bytes.
// This path keeps bits monochrome. Text/background colors are chosen later.
template<class E> void RealizeMono8(E &e, U descriptor, U brush, U origin_x, U origin_y)
{
    e.Write8(brush, e.Read8(brush) | 0x0c);
    U bits = e.ReadPointer(descriptor + 0x0a);
    const U stride = e.Read16(descriptor + 6);
    for (U i = 0; i < 8; ++i, bits += stride)
        e.Write8(brush + 0x0e + i, e.Read8(bits));
    if ((origin_x & 7) || (origin_y & 7)) RotateMono8(e, brush + 0x0e, origin_x, origin_y);
    ExpandMono8(e, brush + 0x0e);
}
} // namespace Win98PatternBrush
