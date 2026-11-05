// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 SE KERNEL32 heap creation and initialization.
#pragma once
namespace Win98HeapCreate
{
using U = unsigned;
struct State
{
    U private_head = 0;
    bool critical_sections_ready = false;
};
// Native providers own address reservations, committed backing, synchronization
// and checked memory access. Guest addresses and header fields remain DWORDs.
// Create: Error, Reserve(page,count,flags), InitializeHeap(root,base,bytes,flags),
// Release(base,flags), MakeCriticalSectionGlobal(address), Write32(address,value), including error paths and private/shared heap registration.
template <class E> U Create(E &e, State &s, U flags, U initial, U maximum)
{
    if (maximum && maximum < initial)
    {
        e.Error(87);
        return 0;
    }
    U size = (maximum + 4095u) & ~4095u;
    if (!size)
    {
        flags |= 0x40;
        size = (initial & ~4095u) + 0x100000u;
    }
    const bool shared = flags & 0x04000000;
    const U base = e.Reserve(shared ? 0x80060000u : 0x80000400u, size >> 12,
                             ((flags & 0x80) >> 4) | 0x10);
    if (base == ~0u)
    {
        e.Error(8);
        return 0;
    }
    const U result = e.InitializeHeap(base, base, size, flags & 0x45);
    if (!result)
    {
        e.Release(base, 0x10);
        return 0;
    }
    if (shared)
    {
        if (s.critical_sections_ready)
            e.MakeCriticalSectionGlobal(base + 0x50);
    }
    else
    {
        e.Write32(base + 0x48, s.private_head);
        s.private_head = base;
    }
    return result;
}
// Merge a following free block, optionally decommit its
// interior, then insert by size into one of the four linked free-block bins.
template <class E> void InsertFree(E &e, U heap, U block, U bytes, U flags)
{
    U following = block + bytes, decommit_limit = 0x100000;
    const U tag = e.Read32(following);
    if (tag & 1)
    {
        bytes += tag & 0x0ffffffc;
        e.Write32(e.Read32(following + 4) + 8, e.Read32(following + 8));
        e.Write32(e.Read32(following + 8) + 4, e.Read32(following + 4));
        decommit_limit = (following + 0x100b) >> 12;
        following = block + bytes;
    }
    if (flags & 0x200)
    {
        const U first = (block + 0x100b) >> 12;
        U last = (following - 4) >> 12;
        if (last >= decommit_limit)
            last = decommit_limit;
        const U count = last - first;
        if (count && !(count & 0x80000000))
            e.Decommit(first, count, 0x20000000);
    }
    e.Write32(following - 4, block);
    e.Write8(following, e.Read8(following) | 2);
    U bin = heap + 8;
    while (e.Read32(bin) < bytes)
        bin += 0x10;
    U previous = bin + 4;
    for (;;)
    {
        const U next = e.Read32(previous + 4), size = e.Read32(next) & 0x0ffffffc;
        if (!size || size >= bytes)
            break;
        previous = next;
    }
    const U next = e.Read32(previous + 4);
    e.Write32(block + 4, next);
    e.Write32(next + 8, block);
    e.Write32(block + 8, previous);
    e.Write32(previous + 4, block);
    e.Write32(block, bytes | 0xa0000001u);
}
// Commit and initialize a root heap or extension region.
// Commit(page,count,heap_flags) is the provider boundary. InitializeLock
// providers supply native synchronization while writing its referenced fields.
template <class E> U Initialize(E &e, const State &s, U heap, U base, U size, U flags)
{
    if (!e.Commit(base >> 12, 1, flags))
        return 0;
    U header = 8;
    if (!(flags & 0x100))
    {
        header = 0x74;
        e.Write32(base + 0x48, 0);
        e.Write32(base + 4, 0);
        e.Write16(base + 0x72, 0x4948);
        e.Write8(base + 0x70, flags);
        if (!(flags & 1))
        {
            if (s.critical_sections_ready)
            {
                e.Write8(base + 0x50, 0);
                e.InitializeCriticalSection(base + 0x50);
                e.Write32(base + 0x4c, e.Read32(base + 0x54));
            }
            else
            {
                e.Write32(base + 0x4c, base + 0x50);
                e.InitializeEarlyLock(base + 0x50);
            }
        }
        constexpr U sizes[] = {32, 128, 512, ~0u};
        for (U i = 0; i < 4; ++i)
        {
            const U bin = base + 8 + i * 16;
            e.Write32(bin, sizes[i]);
            e.Write32(bin + 4, 0xa0000001);
            e.Write32(bin + 8, bin + 0x14);
            e.Write32(bin + 12, bin - 12);
        }
        e.Write32(base + 0x14, base + 0x3c);
        e.Write32(base + 0x40, base + 0xc);
    }
    e.Write32(base, size);
    const U last = base + size - 4;
    if (size > 4096 && !e.Commit(last >> 12, 1, flags))
    {
        e.Decommit(base >> 12, 1, 0x20000000);
        return 0;
    }
    e.Write32(last, 0xa0000000);
    U free_bytes = size - header - 4;
    if (!(flags & 0x400) && size > 4096)
    {
        free_bytes = size - header - 0xffc;
        const U tail = base + free_bytes + header;
        e.Write32(tail, 0xa0000004);
        InsertFree(e, heap, tail + 4, 0xff4, 0);
    }
    InsertFree(e, heap, base + header, free_bytes, 0);
    return base;
}
// after the public HeapDestroy pointer-probe wrapper.
// Validation/locking and lock-object cleanup are native synchronization providers.
template <class E> U Destroy(E &e, State &s, U heap)
{
    e.EnterKernel();
    const U valid = e.ValidateHeap(heap, 0, 0);
    if (valid)
    {
        e.UnlockHeap(heap, 0);
        if (!(e.Read8(heap + 0x70) & 1))
        {
            const U lock = e.Read32(heap + 0x4c);
            if (e.PrimaryHeap() == heap)
                e.ClosePrimaryLock(lock);
            else if (e.Read8(lock) == 4)
                e.DeleteHeapLock(lock);
        }
        if (heap < 0x7fffffff)
        {
            if (s.private_head == heap)
                s.private_head = e.Read32(heap + 0x48);
            else
            {
                U previous = s.private_head;
                while (e.Read32(previous + 0x48) != heap)
                    previous = e.Read32(previous + 0x48);
                e.Write32(previous + 0x48, e.Read32(heap + 0x48));
            }
        }
        for (U region = heap; region;)
        {
            const U next = e.Read32(region + 4);
            e.Release(region, 0x10);
            region = next;
        }
    }
    e.LeaveKernel();
    return valid;
}
} // namespace Win98HeapCreate
