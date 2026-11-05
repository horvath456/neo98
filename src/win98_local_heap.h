// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later
// Win16 local-heap allocation, compaction, handle, and lock behavior.
#pragma once
namespace Win98LocalHeap
{
static_assert(sizeof(unsigned short) == 2, "Win98 WORD width");
struct Environment
{
    unsigned char *data;
    explicit Environment(unsigned char *p) : data(p) {}
    virtual ~Environment() = default;
    unsigned short W(unsigned p) const
    {
        p &= 65535;
        return data[p] | data[(p + 1) & 65535] << 8;
    }
    void W(unsigned p, unsigned v)
    {
        p &= 65535;
        data[p] = v;
        data[(p + 1) & 65535] = v >> 8;
    }
    unsigned char B(unsigned p) const { return data[p & 65535]; }
    void B(unsigned p, unsigned v) { data[p & 65535] = v; }
    // Original Windows 98 returns the size and address of its largest free block
    // after optionally moving/discarding blocks. The caller retains requested.
    virtual unsigned Compact(unsigned requested, unsigned &block) = 0;
    // Original Windows 98 calls the local heap's far notification callback.
    virtual unsigned Notify(unsigned reason, unsigned handle, unsigned size) = 0;
    virtual void Error(unsigned code) = 0;
};
inline unsigned U(unsigned n) { return n & 65535; }
// four-byte rounding, ten-byte minimum and saturation on carry.
inline unsigned Round(unsigned n, bool carry = false)
{
    unsigned v = carry ? 65532 : U(n);
    if (v < 10)
        v = 10;
    unsigned r = U(v + 3) & ~3u;
    return r < v ? 65532 : r;
}
// insert a free block by increasing address, optionally starting
// after a known predecessor. Head and tail sentinels are in the same list.
inline void Insert(Environment &e, unsigned block, unsigned predecessor = 0)
{
    if (!predecessor)
    {
        predecessor = e.W(e.W(6) + 6);
        while (unsigned next = e.W(predecessor + 8))
        {
            if (block < next)
                break;
            predecessor = next;
        }
    }
    unsigned successor = e.W(predecessor + 8);
    e.W(block + 8, successor);
    e.W(predecessor + 8, block);
    e.W(block + 6, predecessor);
    if (successor)
        e.W(successor + 6, block);
    e.W(block + 4, e.W(block + 2) - block);
}
// unlink a free block without changing its physical neighbours.
inline void Remove(Environment &e, unsigned block)
{
    unsigned predecessor = e.W(block + 6), successor = e.W(block + 8);
    e.W(predecessor + 8, successor);
    if (successor)
        e.W(successor + 6, predecessor);
}
// merge block into its physical predecessor. The predecessor
// can still be allocated, e.g. an in-place enlargement in LocalReAlloc.
inline unsigned Join(Environment &e, unsigned block, unsigned predecessor)
{
    unsigned heap = e.W(6), successor = e.W(block + 2);
    e.W(heap + 4, e.W(heap + 4) - 1);
    e.W(successor, (e.W(successor) & 3) | predecessor);
    e.W(predecessor + 2, successor);
    if (!(e.W(block) & 1))
        Remove(e, block);
    if (!(e.W(predecessor) & 1))
        e.W(predecessor + 4, successor - predecessor);
    return successor;
}
// first-fit allocation, low-address fixed / high-address
// movable blocks; a successful split creates a second physical block.
inline unsigned AllocateBlock(Environment &e, unsigned flags, unsigned bytes)
{
    const unsigned heap = e.W(6), header = (flags & 2) ? 6 : 4;
    const unsigned required = Round(bytes + header, bytes + header > 65535);
    for (;;)
    {
        unsigned block = e.W(heap + ((flags & 2) ? 10 : 6));
        for (;;)
        {
            unsigned next = e.W(block + ((flags & 2) ? 6 : 8));
            if (next == block)
            {
                unsigned largest = e.Compact(required, block);
                if (largest >= required)
                    break;
                if (e.Notify(0, largest, required))
                    goto retry;
                return 0;
            }
            block = next;
            if (e.W(block + 4) >= required)
                break;
        }
        {
            unsigned split;
            bool do_split;
            if (flags & 2)
            {
                const unsigned remaining = U(e.W(block + 4) - required);
                split = U(block + remaining);
                do_split = remaining >= 16;
            }
            else
            {
                split = U(block + required);
                do_split = split < U(e.W(block + 2) - 16);
            }
            if (do_split)
            {
                unsigned old_next = e.W(block + 2);
                e.W(block + 2, split);
                e.W(split, block);
                e.W(split + 2, old_next);
                e.W(old_next, (e.W(old_next) & 3) | split);
                e.W(heap + 4, e.W(heap + 4) + 1);
                Insert(e, split, block);
                e.W(block + 4, split - block);
                if (flags & 2)
                    block = split;
            }
            Remove(e, block);
            unsigned result = U(block + header);
            e.B(block, e.B(block) | 1);
            if (flags & 0x40)
            {
                unsigned end = e.W(block + 2);
                // Original rep stosw clears rounded allocation padding too.
                for (unsigned p = result; p + 1 < end; p += 2)
                    e.W(p, 0);
            }
            return result;
        }
    retry:;
    }
}
// return movable handle (or zero), preserving payload except
// words reused as free-list metadata; coalesce next then previous block.
inline unsigned FreeBlock(Environment &e, unsigned block)
{
    if (!block)
        return 0;
    unsigned handle = (e.W(block) & 2) ? e.W(block + 4) : 0;
    Insert(e, block);
    e.W(block, e.W(block) & ~3u);
    unsigned next = e.W(block + 2);
    if (!(e.B(next) & 1))
        Join(e, next, block);
    unsigned previous = e.W(block);
    if (!(e.B(previous) & 1))
        Join(e, block, previous);
    return handle;
}
// Allocate another movable-handle table and
// initialize a four-byte free-handle chain ending at zero. Tables themselves
// are fixed local-heap allocations and remain on heap+0x14's table chain.
inline unsigned GrowHandles(Environment &e, unsigned count)
{
    const unsigned heap = e.W(6);
    unsigned table = AllocateBlock(e, 0, U((count + 1) * 4));
    if (!table)
        return 0;
    unsigned previous = e.W(heap + 20);
    e.W(heap + 20, table);
    e.W(table, count);
    unsigned p = U(table + 2);
    for (unsigned n = 0; n < count; ++n, p = U(p + 4))
    {
        e.W(p, p + 4);
        e.W(p + 2, 65535);
    }
    e.W(p - 4, 0);
    e.W(p, previous);
    return U(table + 2);
}

inline unsigned AllocateHandle(Environment &e, unsigned initial)
{
    const unsigned heap = e.W(6);
    unsigned handle = e.W(heap + 22);
    if (!handle)
    {
        unsigned count = e.W(heap + 24);
        if (!count || !(handle = GrowHandles(e, count)))
            return 0;
    }
    e.W(handle + 2, 0);
    unsigned next = e.W(handle);
    e.W(handle, initial);
    e.W(heap + 22, next);
    return handle;
}

inline unsigned FreeHandle(Environment &e, unsigned handle)
{
    if (!handle)
        return 0;
    unsigned old = e.W(handle + 2);
    e.W(handle + 2, 65535);
    if (old == 65535)
        return 65535;
    unsigned heap = e.W(6), next = e.W(heap + 22);
    e.W(heap + 22, handle);
    e.W(handle, next);
    return 0;
}
// Release a local block, resolving movable handles first. `status` is what
// Windows 98 leaves in CX: zero for a freed handle and for a fixed block, and
// otherwise the lock count in the high byte with the discard bit 40h in the
// low one. LocalReAlloc reads both halves, so they are answered here.
inline unsigned Resolve(Environment &e, unsigned value, unsigned &handle, unsigned &block,
                        unsigned &status)
{
    handle = 0;
    status = 0;
    if (value & 2)
    {
        handle = value;
        unsigned word = e.W(handle + 2);
        if (word != 65535)
            status = (word & 0xff00) | (word & 0x40);
        if (word == 65535 || (word & 0x40))
            return 0;
        value = e.W(handle);
        if (value)
            block = U(value - 6);
    }
    else if (value)
        block = U((value & ~3u) - 4);
    return value;
}
inline unsigned Resolve(Environment &e, unsigned value, unsigned &handle, unsigned &block)
{
    unsigned status = 0;
    return Resolve(e, value, handle, block, status);
}
// Public handle queries, KRNL386. The byte layout is
// flags at handle+2 and lock count at handle+3.
inline unsigned Lock(Environment &e, unsigned value)
{
    if (!(value & 2)) return value;
    if (!(e.B(value + 2) & 0x40) && e.B(value + 3) != 255)
        e.B(value + 3, e.B(value + 3) + 1);
    return e.W(value);
}
inline unsigned Unlock(Environment &e, unsigned value)
{
    if (!(value & 2) || (e.B(value + 2) & 0x40)) return 0;
    const unsigned next = (e.B(value + 3) - 1) & 255;
    if (next >= 254) return 0;
    e.B(value + 3, next);
    return (unsigned)(int)(signed char)next & 65535;
}
inline unsigned Flags(Environment &e, unsigned value)
{
    const unsigned flags = value & 2 ? e.W(value + 2) : 0;
    return ((flags << 8) | (flags >> 8)) & 65535;
}
inline unsigned Handle(Environment &e, unsigned pointer)
{
    if (!(pointer & 2)) return pointer;
    const unsigned handle = e.W(pointer - 2);
    return e.W(handle) == pointer ? handle : 0;
}
// The complete allocation entry. Public 's selector checks
// are outside this port; callers must supply a valid initialized local heap.
inline unsigned Allocate(Environment &e, unsigned flags, unsigned bytes)
{
    flags = U(flags);
    bytes = U(bytes);
    const unsigned heap = e.W(6), busy = e.W(heap + 34);
    e.W(heap + 34, 1);
    unsigned result = 0;
    if (!busy)
    {
        if (flags & 0x10)
            e.W(heap + 2, e.W(heap + 2) + 1);
        if (!bytes)
        {
            if (flags & 2)
            {
                result = AllocateHandle(e, 2);
                if (result)
                {
                    e.B(result, e.B(result) ^ 2);
                    e.B(result + 2, e.B(result + 2) | 0x40);
                }
            }
        }
        else if (flags & 2)
        {
            result = AllocateHandle(e, flags);
            if (result)
            {
                unsigned address = AllocateBlock(e, flags, bytes);
                if (!address)
                {
                    FreeHandle(e, result);
                    result = 0;
                }
                else
                {
                    e.W(result, address);
                    e.W(address - 2, result);
                    e.B(address - 6, e.B(address - 6) | 2);
                    if ((flags >> 8) & 15)
                        e.B(result + 2, (flags >> 8) & 15);
                }
            }
        }
        else
            result = AllocateBlock(e, flags, bytes);
        if (flags & 0x10)
            e.W(heap + 2, e.W(heap + 2) - 1);
        e.W(heap + 34, 0);
    }
    if (!result)
        e.Error(4);
    return result;
}
// Complete internal LocalFree.
inline unsigned Free(Environment &e, unsigned value)
{
    value = U(value);
    unsigned heap = e.W(6), busy = e.W(heap + 34);
    e.W(heap + 34, 1);
    if (busy)
        return 0;
    unsigned handle = 0, block = 0;
    if (Resolve(e, value, handle, block))
        handle = FreeBlock(e, block);
    unsigned result = FreeHandle(e, handle);
    e.W(heap + 34, 0);
    return result;
}
// without the busy flag and the 10h counter, which ReAllocate
// keeps around it. `failed` is the path, which tells the debug
// notification of segment one and answers zero.
inline unsigned ReAllocateCore(Environment &e, unsigned value_in, unsigned bytes, unsigned flags,
                               bool &failed)
{
    const unsigned heap = e.W(6);
    unsigned handle = 0, block = 0, status = 0;
    const unsigned value = Resolve(e, value_in, handle, block, status);
    // hang a freshly allocated block on the handle again.
    auto attach = [&](unsigned address) {
        e.W(handle, address);
        e.B(U(address - 6), e.B(U(address - 6)) | 2);
        e.W(U(address - 2), handle);
        return handle;
    };
    if (!value)
    {
        // nothing is behind the handle. A freed handle comes back
        // unchanged; a discarded one gets a new block.
        if (!(status & 0x40))
            return value_in;
        const unsigned address = AllocateBlock(e, flags | 2, bytes);
        if (!address)
        {
            failed = true;
            return 0;
        }
        e.B(handle + 2, e.B(handle + 2) ^ 0x40);
        return attach(address);
    }
    if (flags & 0x80)
    {
        // LMEM_MODIFY: only the five low status bits of the handle
        // change, and a fixed block has no handle to change them on.
        if (handle)
            e.B(handle + 2, (e.B(handle + 2) & 0xe0) | ((flags >> 8) & 0x1f));
        return value_in;
    }
    // the end address the block needs, rounded the way rounds.
    const unsigned request = bytes < 10 ? 10u : bytes;
    const unsigned want = Round(U(value + request), value + request > 65535);
    unsigned end = e.W(block + 2);
    if (!bytes)
    {
        // LocalReAlloc(handle, 0, LMEM_MOVEABLE) discards the block.
        // A locked handle keeps it; the lock count is the high byte of status.
        if (status || !(flags & 2) || !e.Notify(2, value_in, 0))
        {
            failed = true;
            return 0;
        }
        const unsigned freed = FreeBlock(e, block);
        if (!freed)
            return 0;
        e.W(freed, 0);
        e.B(freed + 2, e.B(freed + 2) | 0x40);
        return value_in;
    }
    if (want > end)
    {
        // the block grows. A free physical successor that is long
        // enough is joined in place, otherwise the block moves.
        if (!(e.B(end) & 1) && want <= e.W(end + 2))
        {
            const unsigned successor = Join(e, end, block);
            if (flags & 0x40)
                for (unsigned p = end, n = U(successor - end) >> 1; n; --n, p = U(p + 2))
                    e.W(p, 0);
            end = successor;
        }
        else
        {
            // A locked block may only move when the caller allows it,
            // and a fixed handle gets a fixed block again.
            unsigned moved = flags | 2;
            if (status && !(flags & 2))
            {
                failed = true;
                return 0;
            }
            if (!(value_in & 2))
            {
                if (!(flags & 2))
                {
                    failed = true;
                    return 0;
                }
                moved = (flags | 2) ^ 2;
            }
            const unsigned carried = U(e.W(block + 2) - value);
            const unsigned address = AllocateBlock(e, moved, bytes);
            if (!address)
            {
                failed = true;
                return 0;
            }
            e.Notify(1, value_in, address);
            // the notification may have moved things, so the source is
            // resolved again before the copy.
            unsigned again_handle = 0, again_block = 0, again_status = 0;
            const unsigned source = Resolve(e, value_in, again_handle, again_block, again_status);
            for (unsigned i = 0, n = carried >> 1; i < n; ++i)
                e.W(U(address + i * 2), e.W(U(source + i * 2)));
            const unsigned freed = FreeBlock(e, again_block);
            if (!freed)
                return address;
            handle = freed;
            return attach(address);
        }
    }
    // give the surplus at the end back, but only when at least
    // sixteen bytes are left over. Windows 98 answers the handle when the sum
    // carries out of sixteen bits.
    if (want + 16 <= 65535 && want + 16 < end)
    {
        const unsigned split = want;
        unsigned successor = e.W(block + 2);
        e.W(block + 2, split);
        e.W(split, block);
        e.W(successor, e.W(successor) & 3);
        if (e.W(successor))
            e.W(heap + 4, e.W(heap + 4) + 1);
        else
        {
            // a free successor is swallowed by the new tail block.
            Remove(e, successor);
            successor = e.W(successor + 2);
            e.W(successor, e.W(successor) & 3);
        }
        e.W(successor, e.W(successor) | split);
        e.W(split + 2, successor);
        Insert(e, split, 0);
    }
    return value_in;
}
// The complete reallocation entry. The public only
// rejects the flag bits F00Dh before it jumps here.
inline unsigned ReAllocate(Environment &e, unsigned value_in, unsigned bytes, unsigned flags)
{
    value_in = U(value_in);
    bytes = U(bytes);
    flags = U(flags);
    const unsigned heap = e.W(6), busy = e.W(heap + 34);
    e.W(heap + 34, 1);
    if (busy)
        return 0;
    if (flags & 0x10)
        e.W(heap + 2, e.W(heap + 2) + 1);
    bool failed = false;
    unsigned result = ReAllocateCore(e, value_in, bytes, flags, failed);
    if (failed)
    {
        e.Error(5);
        result = 0;
    }
    if (flags & 0x10)
        e.W(heap + 2, e.W(heap + 2) - 1);
    e.W(heap + 34, 0);
    return result;
}
// The growth the default local notification of achieves: it enlarges
// the automatic data segment with GlobalReAlloc and moves the tail sentinel of
// the heap into the new space. This port keeps the whole 64 KiB of the segment
// behind the descriptor from the start, so only the tail moves. The old
// sentinel becomes the free block that covers the new space and merges with
// its physical predecessor when that one is free as well.
inline bool Grow(Environment &e, unsigned end)
{
    const unsigned heap = e.W(6), tail = e.W(heap + 10);
    const unsigned grown = U((end - 10) & ~3u);
    if (end < 10 || grown <= tail)
        return false;
    const unsigned previous = e.W(tail) & ~3u;
    e.W(tail, previous);
    e.W(tail + 2, grown);
    e.W(tail + 4, U(grown - tail));
    e.W(tail + 8, grown);
    e.W(grown, tail | 1);
    e.W(grown + 2, grown);
    e.W(grown + 4, 10);
    e.W(grown + 6, tail);
    e.W(grown + 8, grown);
    e.W(heap + 10, grown);
    e.W(heap + 4, e.W(heap + 4) + 1);
    e.W(heap + 38, U(grown + 10 - e.W(heap + 6)));
    if (previous && !(e.B(previous) & 1))
        Join(e, tail, previous);
    return true;
}
// Initialization core. End/start are already supplied;
// the original GlobalSize fallback, GlobalUnlock and selector handling are
// providers outside this port. The packed arena and callback offsets retain
// the original bytes so full-state comparison remains possible.
inline bool Initialize(Environment &e, unsigned start, unsigned end)
{
    start = U(start);
    end = U(end);
    unsigned head = Round(start), initial = Round(head + 10), heap = U(initial + 4);
    if (initial >= end)
        return false;
    for (unsigned p = heap; p + 1 < end; p += 2)
        e.W(p, 0);
    e.B(heap + 24, 32);
    e.B(heap + 4, 4);
    e.W(heap + 6, head);
    e.W(heap + 30, 0x3437);
    e.W(heap + 32, 0x10);
    e.W(heap + 26, 0x309a);
    e.W(heap + 36, 0x200);
    e.W(heap + 40, 0x484c);
    unsigned free = Round(heap + 42), tail = U(end - 10) & ~3u;
    if (tail <= free)
        return false;
    e.W(heap + 10, tail);
    e.W(6, heap);
    e.W(head, head | 1);
    e.W(head + 2, initial);
    e.W(head + 6, head);
    e.W(head + 8, free);
    e.W(head + 4, 10);
    e.W(initial, head | 1);
    e.W(initial + 2, free);
    e.W(free, initial);
    e.W(free + 2, tail);
    e.W(free + 6, head);
    e.W(free + 8, tail);
    e.W(free + 4, tail - free);
    e.W(tail + 2, tail);
    e.W(tail, free | 1);
    e.W(tail + 6, free);
    e.W(tail + 8, tail);
    e.W(tail + 4, 10);
    e.W(heap + 38, tail + 10 - head);
    return true;
}
} // namespace Win98LocalHeap
