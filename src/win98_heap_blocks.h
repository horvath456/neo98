// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 SE KERNEL32 heap block allocator.
#pragma once
namespace Win98HeapBlocks
{
using U = unsigned;
constexpr U SizeMask = 0x0ffffffc;
// Providers: Read8/Read16/Read32, Write8/Write32, Zero, Copy, Error;
// EnterCritical/LeaveCritical, Lock(lockAddress)/Unlock(lockAddress);
// CommitPages(page,count,pager,key,flags), Reserve(arena,pages,flags), Release(base,flags);
// InitializeHeap(root,region,size,flags), InsertFree(root,block,size,flags);
// CompatFlags, ProcessHeap, RaiseNoMemory(size). Guest addresses remain DWORDs.


template <class E> void Unlock(E &e, U heap, U flags)
{
    if (!((e.Read8(heap + 0x70) | flags) & 1))
    {
        e.Unlock(e.Read32(heap + 0x4c));
        e.LeaveCritical();
    }
}
// Success holds the lock unless NO_SERIALIZE is active.
template <class E> bool Validate(E &e, U heap, U block, U flags)
{
    if (e.Read16(heap + 0x72) != 0x4948) { e.Error(87); return false; }
    if (!((e.Read8(heap + 0x70) | flags) & 1))
    {
        e.EnterCritical();
        e.Lock(e.Read32(heap + 0x4c));
    }
    if (!block) return true;
    for (U region = heap; region; region = e.Read32(region + 4))
        if (region < block && region + e.Read32(region) > block)
        {
            if ((e.Read32(block) & 0xf0000001u) == 0xa0000000u) return true;
            break;
        }
    e.Error(87);
    Unlock(e, heap, flags);
    return false;
}
// Nonpositive signed page counts are successful no-ops.
template <class E> U Commit(E &e, U page, U count, U flags)
{
    if (!count || (count & 0x80000000u)) return 1;
    const U result = e.CommitPages(page, count, 2 - ((flags & 0x40) >> 6), 0, 0x20060000);
    if (!result) e.Error(8);
    return result;
}
// Remove a free block, split when >=16 bytes remain,
// commit its needed pages, and perform the original zero-fill policy.
template <class E> U TakeBlock(E &e, U heap, U block, U wanted, U flags)
{
    U available = e.Read32(block) & SizeMask;
    const bool zero = flags & 0x40;
    const U page_zero = zero && wanted > 0x4000 ? 0x40 : 0;
    const U first = (block + 0x100b) >> 12, last = (block + available - 4) >> 12;
    if (wanted + 16 <= available)
    {
        U end = (block + wanted + 0xb) >> 12;
        if (end == last) --end;
        if (!Commit(e, first, end - first + 1, e.Read8(heap + 0x70) | page_zero)) return 0;
        e.Write32(e.Read32(block + 4) + 8, e.Read32(block + 8));
        e.Write32(e.Read32(block + 8) + 4, e.Read32(block + 4));
        e.InsertFree(heap, block + wanted, available - wanted, 0);
    }
    else
    {
        if (!Commit(e, first, last - first, e.Read8(heap + 0x70) | page_zero)) return 0;
        wanted = available;
        e.Write32(e.Read32(block + 4) + 8, e.Read32(block + 8));
        e.Write32(e.Read32(block + 8) + 4, e.Read32(block + 4));
        e.Write8(block + available, e.Read8(block + available) & ~2u);
    }
    if (zero)
    {
        if (page_zero)
        {
            e.Zero(block, (first << 12) - block);
            const U tail = last << 12;
            if (block + wanted > tail) e.Zero(tail, block + wanted - tail);
        }
        else e.Zero(block, wanted);
    }
    return wanted;
}
// Internal flag 40h requests zeroed memory.
template <class E> U Allocate(E &e, U heap, U bytes, U flags)
{
    if (bytes > 0x0fffff98) { e.Error(8); return 0; }
    if (!Validate(e, heap, 0, flags)) return 0;
    U needed = (bytes + 7) & ~3u;
    if (needed <= 16) needed = 16;
    for (;;)
    {
        U bin = heap + 8;
        while (e.Read32(bin) < needed) bin += 16;
        U block = e.Read32(bin + 8);
        while (block != heap + 0xc)
        {
            if ((e.Read32(block) & SizeMask) >= needed)
            {
                needed = TakeBlock(e, heap, block, needed, flags);
                if (needed)
                {
                    e.Write32(block, needed | 0xa0000000u);
                    Unlock(e, heap, flags);
                    return block + 4;
                }
                goto failed;
            }
            block = e.Read32(block + 4);
        }
        if (!(e.Read8(heap + 0x70) & 0x40)) { e.Error(8); goto failed; }
        U size = (needed + 0x2000) & ~4095u;
        if (size <= 0x400000) size = 0x400000;
        const U region = e.Reserve(heap < 0x80000000 ? 0x80000400u : 0x80060000u, size >> 12, 0x10);
        if (region == ~0u) { e.Error(8); goto failed; }
        if (!e.InitializeHeap(heap, region, size, e.Read8(heap + 0x70) | 0x100u))
        {
            e.Release(region, 0x10);
            goto failed;
        }
        e.Write32(region + 4, e.Read32(heap + 4));
        e.Write32(heap + 4, region);
    }
failed:
    Unlock(e, heap, flags);
    if ((e.Read8(heap + 0x70) | flags) & 4) e.RaiseNoMemory(needed);
    return 0;
}
// the ordinary target of 's compatibility thunk.
template <class E> U HeapFree(E &e, U heap, U flags, U pointer)
{
    U block = pointer - 4;
    if (!Validate(e, heap, block, flags)) return 0;
    U tag = e.Read32(block), bytes = tag & SizeMask;
    e.Write32(block, tag | 0xf0000000u);
    if (tag & 2)
    {
        block = e.Read32(block - 4);
        bytes += e.Read32(block) & SizeMask;
        e.Write32(e.Read32(block + 4) + 8, e.Read32(block + 8));
        e.Write32(e.Read32(block + 8) + 4, e.Read32(block + 4));
    }
    e.InsertFree(heap, block, bytes, 0x200);
    Unlock(e, heap, flags);
    return 1;
}
// Returns the actual payload capacity, zero on failure.
template <class E> U HeapSize(E &e, U heap, U flags, U pointer)
{
    if (!Validate(e, heap, pointer - 4, flags)) return 0;
    const U tag = e.Read32(pointer - 4);
    Unlock(e, heap, flags);
    return (tag & SizeMask) - 4;
}
// Internal bit 2 permits moving; bit 20h discards old contents.
template <class E> U Reallocate(E &e, U heap, U pointer, U bytes, U flags)
{
    if (bytes > 0x0fffff98) { e.Error(8); return 0; }
    const U block = pointer - 4;
    if (!Validate(e, heap, block, flags)) return 0;
    U wanted = (bytes + 7) & ~3u;
    if (wanted <= 16) wanted = 16;
    const U old = e.Read32(block) & SizeMask;
    if (wanted + 16 <= old)
    {
        e.InsertFree(heap, block + wanted, old - wanted, 0x200);
        e.Write32(block, (e.Read32(block) & 0xf0000003u) | wanted);
    }
    else if (old < wanted)
    {
        const U next = block + old, tag = e.Read32(next);
        if ((tag & 1) && (tag & SizeMask) >= wanted - old)
        {
            const U added = TakeBlock(e, heap, next, wanted - old, flags);
            if (!added) goto failed;
            e.Write32(block, (e.Read32(block) & 0xf0000003u) | (old + added));
        }
        else if (flags & 2)
        {
            wanted -= 4;
            if (flags & 0x20)
            {
                HeapFree(e, heap, 1, pointer);
                pointer = Allocate(e, heap, wanted, flags | 1);
                if (!pointer) goto failed;
            }
            else
            {
                const U replacement = Allocate(e, heap, wanted, flags | 1);
                if (!replacement) goto failed;
                e.Copy(replacement, pointer, old - 4);
                HeapFree(e, heap, 1, pointer);
                pointer = replacement;
            }
        }
        else { e.Error(212); goto failed; }
    }
    Unlock(e, heap, flags);
    return pointer;
failed:
    Unlock(e, heap, flags);
    return 0;
}
// Public HeapAlloc converts 8h to internal zero flag 40h.
template <class E> U HeapAlloc(E &e, U heap, U flags, U bytes)
{
    if ((e.CompatFlags() & 0x400000) && e.ProcessHeap() == heap) bytes += 16;
    return Allocate(e, heap, bytes, (flags & 4) | ((flags & 8) << 3));
}
// NO_SERIALIZE, exception, zero and IN_PLACE_ONLY conversion.
template <class E> U HeapReAlloc(E &e, U heap, U flags, U pointer, U bytes)
{
    const U internal = (flags & 5) | ((flags & 8) << 3) | ((flags & 0x10) ? 0 : 2);
    return Reallocate(e, heap, pointer, bytes, internal);
}
} // namespace Win98HeapBlocks
