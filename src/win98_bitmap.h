// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 GDI software DDB storage and bit transfer.
#pragma once
namespace Win98Bitmap
{
using U = unsigned;
static_assert(sizeof(U) == 4, "32-bit original arithmetic");
struct State { U empty_bitmap = 0, empty_storage = 0, serial = 0; };
struct Bitmap { U type, width, height, stride, planes, bpp, bits; };
struct DeviceResult { U value, count; }; // Original DX:AX and preserved/changed CX.
// All memory addresses are provider tokens, with flat byte-offset arithmetic.
// ReadPointer/WritePointer adapt the stored 16:16 pointer at DDB+0Ah. Handles
// stored in original WORD fields must fit 16 bits; public handles are external.
// Providers: Read8/16/32, Write8/16/32, ReadPointer/WritePointer; Object(handle),
// LocalAllocate(flags,bytes), MarkLocal(handle,mask), LocalFree(handle);
// GlobalAllocate(flags,bytes), GlobalLock/Unlock/Free, GlobalSmartPageLock;
// GetCurrentTask, GetExpectedVersion(task); DeviceCapable(hdc),
// RealizeBitmap(hdc,bitmap,height)->DeviceResult, DeviceHandle/DeviceTable(hdc),
// DriverBits(table,bitmap,mode,count,bits), DestroyDevice(table,device,bitmap);
// ProbeBits(bits,count), FreeSelector(handle), DibSectionCleanup(object),
// ArithmeticFault. Pointer validation, storage limits and synchronization are
// provider responsibilities. ArithmeticFault must report/stop an original #DE,
// not supply a fabricated allocation success. DIBSECTION cleanup is not ported.

// GDI. The descriptor receives its owned bits pointer on each lock.
template<class E> U Lock(E &e, U handle)
{
    if (!handle) return 0;
    const U object = e.Object(handle);
    const U block = e.GlobalLock(e.Read16(object + 0x0e));
    if (!block) return 0;
    if (e.Read16(block) != 0x5250)
    {
        U bits = block + 0x20;
        const U extra = e.Read16(object + 0x14);
        if (extra)
        {
            bits = e.GlobalLock(extra);
            if (!bits) { e.GlobalUnlock(e.Read16(object + 0x0e)); return 0; }
        }
        e.WritePointer(block + 0x0a, bits);
    }
    return block;
}
// GDI.
template<class E> void Unlock(E &e, U handle)
{
    if (!handle) return;
    const U object = e.Object(handle), extra = e.Read16(object + 0x14);
    if (!(e.Read16(object + 0x1e) & 4) && extra) e.GlobalUnlock(extra);
    e.GlobalUnlock(e.Read16(object + 0x0e));
}
// A provider may move a whole block at once when source and destination do
// not overlap; the result is the same as the word loop below. Providers
// without CopyBlock take the loop.
template<class E> auto CopyBlock(E &e, U destination, U source, U bytes, int) -> decltype(e.CopyBlock(destination, source, bytes))
{
    return e.CopyBlock(destination, source, bytes);
}
template<class E> bool CopyBlock(E &, U, U, U, long) { return false; }
// GDI: forward WORD transfers and a final BYTE, including overlap.
template<class E> void Copy(E &e, U &destination, U &source, U bytes)
{
    if ((destination + bytes <= source || source + bytes <= destination) && CopyBlock(e, destination, source, bytes, 0))
    {
        destination += bytes; source += bytes;
        return;
    }
    for (U words = bytes >> 1; words; --words)
    {
        const U value = e.Read16(source);
        e.Write16(destination, value); source += 2; destination += 2;
    }
    if (bytes & 1) { const U value = e.Read8(source); e.Write8(destination, value); ++source; ++destination; }
}
// GDI. Mode 1 writes bits, mode 2 reads them.
template<class E> U Bits(E &e, U handle, U count, U bits, U mode)
{
    if (count >> 16) e.ProbeBits(bits, count); // KERNEL.627 plus selector-limit gate.
    const U object = e.Object(handle);
    if (e.Read16(object + 0x1e) & 6)
    {
        const U block = Lock(e, handle);
        const U result = e.DriverBits(e.Read16(object + 0x1c), block, mode, count, bits);
        Unlock(e, handle);
        return result;
    }
    const U block = Lock(e, handle);
    if (!block) return 0;
    U total = e.Read32(block + 0x0e);
    const U planes = e.Read8(block + 8);
    if (planes != 1) total *= planes;
    if (count > total) count = total;
    U destination = mode == 1 ? e.ReadPointer(block + 0x0a) : bits;
    U source = mode == 1 ? bits : e.ReadPointer(block + 0x0a);
    const U gap = e.Read16(block + 0x16) ? e.Read16(block + 0x1a) : 0;
    const U group = e.Read16(block + 0x16) ? 0x10000u - gap : count;
    U remaining = count;
    // The original calls its copy helper once even for a zero byte count.
    do
    {
        const U part = remaining < group ? remaining : group;
        Copy(e, destination, source, part);
        remaining -= part;
        if (!remaining) break;
        if (mode == 1) destination += gap; else source += gap;
    } while (true);
    Unlock(e, handle);
    return count;
}
template<class E> U SetBits(E &e, U handle, U count, U bits) { return Bits(e, handle, count, bits, 1); }
template<class E> U GetBits(E &e, U handle, U count, U bits) { return Bits(e, handle, count, bits, 2); }

// GDI. Input is the already narrowed BITMAP16 record.
template<class E> U Create(E &e, State &s, const Bitmap &input, U hdc = 0)
{
    const Bitmap b{input.type & 0xffffu, input.width & 0xffffu, input.height & 0xffffu,
                   input.stride & 0xffffu, input.planes & 0xffu, input.bpp & 0xffu, input.bits};
    const U version = e.GetExpectedVersion(e.GetCurrentTask()) & 0xffffu;
    if (!b.width) return s.empty_bitmap;
    if (b.width & 0x8000) return 0;
    if (!b.height) return s.empty_bitmap;
    if (b.height & 0x8000) return 0;
    U height = b.height, primary = 0, extra = 0, segmented = 0, flags = 0, total = 0;
    if (hdc && e.DeviceCapable(hdc))
    {
        const DeviceResult device = e.RealizeBitmap(hdc, b, height);
        height = device.count & 0xffffu;
        const U high = device.value >> 16, low = (device.value | high) & 0xffffu;
        if (low)
        {
            if (high == 0x8000) return 0;
            primary = low; extra = segmented = high; flags = 2;
        }
    }
    if (!primary)
    {
        if (b.planes == 1 && b.bpp == 1) flags |= 8;
        U stride = (((b.width * b.bpp + 15) & 0xffffu) & 0xfff0u) >> 3;
        if (!stride) return 0;
        if (stride < b.stride) stride = b.stride;
        else if (stride > b.stride && b.bits) return 0;
        const U per_plane = stride * height;
        if (!b.planes) return 0;
        total = per_plane * b.planes; // Original repeated ADD/ADC wraps at 32 bits.
        U low = (total + 0x24u) & 0xffffu;
        U high = ((total >> 16) + (((total & 0xffffu) + 0x24u) >> 16)) & 0xffffu;
        bool external = false;
        if (high)
        {
            high = (high + low) & 0xffffu;
            if (high != 1) { external = true; low = 0x20; high = 0; }
        }
        primary = e.GlobalAllocate(0x2002, low | (high << 16));
        if (!primary) return 0;
        if (!s.empty_bitmap)
        {
            if (!e.GlobalSmartPageLock(primary)) { e.GlobalFree(primary); return 0; }
            s.empty_storage = primary;
        }
        const U block = e.GlobalLock(primary);
        if (!block) { e.GlobalFree(primary); return 0; }
        e.Write16(block, 0);
        e.Write16(block + 2, b.width); e.Write16(block + 4, b.height);
        e.Write16(block + 6, b.stride); e.Write16(block + 8, b.planes | (b.bpp << 8));
        e.WritePointer(block + 0x0a, b.bits);
        e.Write32(block + 0x0e, per_plane); e.Write32(block + 0x12, 0);
        segmented = (total >> 16) && (((total >> 16) + (total & 0xffffu) - 1) & 0xffffu);
        e.Write16(block + 0x16, segmented ? 8 : 0); // Original __AHINCR.
        const U row = (stride * b.planes) & 0xffffu;
        if (!row || 0x10000u / row > 0xffffu) { e.ArithmeticFault(); return 0; }
        const U rows = 0x10000u / row, gap = 0x10000u % row;
        e.Write16(block + 0x18, rows); e.Write16(block + 0x1a, gap);
        e.Write32(block + 0x1c, 0);
        if (external)
        {
            U bytes = total;
            if (segmented)
                bytes = ((b.height / rows) << 16) | ((((b.height % rows) * stride) & 0xffffu) * b.planes & 0xffffu);
            total = bytes; // The original reuses its count locals for the padded size.
            extra = e.GlobalAllocate(0x2002, bytes + 4);
            if (!extra) { e.GlobalUnlock(primary); e.GlobalFree(primary); return 0; }
        }
    }
    const U handle = e.LocalAllocate(2, 0x22);
    if (!handle)
    {
        if (extra) e.GlobalFree(extra);
        e.GlobalUnlock(primary); e.GlobalFree(primary);
        return 0;
    }
    const U object = e.Object(handle);
    e.MarkLocal(handle, 0x10);
    e.Write16(object, 0); e.Write16(object + 2, 0x4f4b);
    e.Write32(object + 4, s.serial++);
    e.Write32(object + 8, 0); e.Write16(object + 0x0c, 0);
    if (version < 0x400) e.Write16(object + 0x0a, primary);
    e.Write16(object + 0x0e, primary); e.Write32(object + 0x10, 0);
    e.Write16(object + 0x14, extra); e.Write32(object + 0x16, 0);
    e.Write16(object + 0x1a, flags & 2 ? e.DeviceHandle(hdc) : 0);
    e.Write16(object + 0x1c, flags & 2 ? e.DeviceTable(hdc) : 0);
    e.Write16(object + 0x1e, flags | (segmented ? 1 : 0)); e.Write16(object + 0x20, 0);
    e.GlobalUnlock(primary);
    if (b.bits) SetBits(e, handle, total, b.bits); // Original deliberately ignores failure.
    return handle;
}

// GDI's bitmap-specific DeleteObject dispatch target.
// The generic object validation/dispatch and DIBSECTION cleanup are providers.
template<class E> U Destroy(E &e, const State &s, U handle)
{
    if (handle == s.empty_bitmap) return 1;
    const U object = e.Object(handle);
    if (e.Read16(object + 0x10)) return 1; // In use: success without freeing.
    if (e.Read16(object + 0x1e) & 2)
    {
        const U block = e.GlobalLock(e.Read16(object + 0x0e));
        e.DestroyDevice(e.Read16(object + 0x1c), e.Read16(object + 0x1a), block);
        e.GlobalUnlock(e.Read16(object + 0x0e));
    }
    const U extra = e.Read16(object + 0x14);
    if (extra)
    {
        if (e.Read16(object + 0x1e) & 0x10) e.FreeSelector(extra);
        else e.GlobalFree(extra);
    }
    if (e.Read16(object + 0x1e) & 4) e.DibSectionCleanup(object);
    e.GlobalFree(e.Read16(object + 0x0e));
    e.LocalFree(handle);
    return 1;
}
} // namespace Win98Bitmap
