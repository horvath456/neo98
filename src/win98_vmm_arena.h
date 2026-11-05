// SPDX-License-Identifier: GPL-2.0-or-later
// VMM.VXD c1ed281d4eb807dd76d3253a38fbae5d059e26f5d4ad867a245a68e60be2fd2a.
// Original free-block algorithms with native pointers and separate page/flag fields.
// No original x86 page-table addresses, descriptor indices or privileged instructions.
#pragma once
namespace Win98VmmArena
{
using U = unsigned;
struct Block
{
    U page = 0, pages = 0, flags = 0;
    Block *next = nullptr, *previous = nullptr;
};

// At most N occupied intervals need N+1 free intervals. Space has 256 region
// records and 16 permanent exclusions, so 576 descriptors include spare capacity.
template <U Capacity = 576> class Arena
{
  public:
    explicit Arena(U first_page = 0, U end_page = 0x100000)
    {
        free.page = ~0u;
        free.next = &free;
        allocated.next = allocated.previous = &allocated;
        for (U i = Capacity; i; --i)
            Recycle(&blocks[i - 1]);
        if (first_page < end_page && end_page <= 0x100000)
        {
            if (auto *initial = New())
            {
                initial->page = first_page;
                initial->pages = end_page - first_page;
                initial->next = &free;
                free.next = initial;
            }
        }
    }
    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    // Windows sums the selected arena's free-block lengths.
    // This native store combines arenas, so intersect with the requested bounds.
    U FreePages(U minimum, U maximum) const
    {
        U count = 0;
        for (auto *block = free.next; block != &free; block = block->next) {
            U first = block->page > minimum ? block->page : minimum;
            U end = block->page + block->pages;
            if (end > maximum) end = maximum;
            if (first < end) count += end - first;
        }
        return count;
    }

    bool Available(U page, U pages) const
    {
        if (!pages || page >= 0x100000 || pages > 0x100000 - page)
            return false;
        for (auto *block = free.next; block != &free; block = block->next)
            if (block->page <= page && page - block->page < block->pages &&
                pages <= block->pages - (page - block->page))
                return true;
        return false;
    }

    // _PageReserve THCR scans free descriptors in address order.
    // Bounds and arbitrary power-of-two alignment adapt its arena/PR_4MEG policy
    // to NEO98's guest address space, including page-sized PE mappings.
    U Find(U pages, U alignment, U minimum, U maximum) const
    {
        if (!pages || !alignment || (alignment & (alignment - 1)) || minimum >= maximum || maximum > 0x100000)
            return ~0u;
        for (auto *block = free.next; block != &free; block = block->next)
        {
            U page = block->page > minimum ? block->page : minimum;
            const U end = block->page + block->pages < maximum ? block->page + block->pages : maximum;
            if (page >= end || alignment - 1 > ~0u - page)
                continue;
            page = (page + alignment - 1) & ~(alignment - 1);
            if (page < end && pages <= end - page)
                return page;
        }
        return ~0u;
    }

    // _PageReserve THCR : fixed selection, prefix split, exact
    // removal or second split, and rollback if the second descriptor is unavailable.
    Block *Reserve(U page, U pages, U flags = 0)
    {
        if (!pages || page >= 0x100000 || pages > 0x100000 - page)
            return nullptr;
        Block *previous = &free, *block = free.next;
        const U last = page + pages - 1;
        while (block != &free && block->page + block->pages <= last)
        {
            previous = block;
            block = block->next;
        }
        if (block == &free || block->page > page)
            return nullptr;
        Block *prefix = nullptr;
        if (page > block->page)
        {
            prefix = SplitPrefix(block, page - block->page);
            if (!prefix)
                return nullptr;
            prefix->next = block;
            previous->next = prefix;
            previous = prefix;
        }
        if (block->pages == pages)
            previous->next = block->next;
        else
        {
            auto *reserved = SplitPrefix(block, pages);
            if (!reserved)
            {
                if (prefix)
                {
                    prefix->next = block->next;
                    prefix->pages += block->pages;
                    Recycle(block);
                }
                return nullptr;
            }
            block = reserved;
        }
        // THCR : attach the reservation to the allocated list.
        block->flags = flags;
        block->next = allocated.next;
        block->previous = &allocated;
        allocated.next->previous = block;
        allocated.next = block;
        return block;
    }

    // Complete THDE helper: unlink allocation, sorted insertion,
    // merge with successor, then predecessor, and recycle redundant descriptors.
    Block *Release(Block *block)
    {
        block->next->previous = block->previous;
        block->previous->next = block->next;
        block->flags = 0;
        Block *previous = &free, *next = free.next;
        while (next != &free && next->page < block->page)
        {
            previous = next;
            next = next->next;
        }
        previous->next = block;
        block->next = next;
        if (next != &free && next->page - block->pages == block->page)
        {
            block->pages += next->pages;
            block->next = next->next;
            Recycle(next);
        }
        if (previous != &free && previous->page + previous->pages == block->page)
        {
            previous->pages += block->pages;
            previous->next = block->next;
            Recycle(block);
            block = previous;
        }
        return block;
    }

    const Block *FreeList() const { return &free; }
    const Block *AllocatedList() const { return &allocated; }

  private:
    Block blocks[Capacity]{}, free{}, allocated{};
    Block *unused = nullptr;
    Block *New()
    {
        auto *block = unused;
        if (block)
            unused = block->next;
        return block;
    }
    void Recycle(Block *block)
    {
        block->next = unused;
        unused = block;
    }
    // Complete THCR helper. Allocation failure leaves the old
    // descriptor untouched. The new prefix keeps the old flags and page start.
    Block *SplitPrefix(Block *remainder, U pages)
    {
        auto *prefix = New();
        if (!prefix)
            return nullptr;
        prefix->pages = pages;
        remainder->pages -= pages;
        prefix->page = remainder->page;
        prefix->flags = remainder->flags;
        remainder->page += pages;
        return prefix;
    }
};
} // namespace Win98VmmArena
