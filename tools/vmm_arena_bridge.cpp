// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_vmm_arena.h"
#include <cstdint>
using Arena = Win98VmmArena::Arena<8>;
using Block = Win98VmmArena::Block;
extern "C"
{
    void *vmm_arena_create(unsigned first, unsigned limit) { return new Arena(first, limit); }
    void vmm_arena_destroy(void *context) { delete static_cast<Arena *>(context); }
    std::uintptr_t vmm_arena_reserve(void *context, unsigned page, unsigned pages, unsigned flags)
    {
        return reinterpret_cast<std::uintptr_t>(static_cast<Arena *>(context)->Reserve(page, pages, flags));
    }
    unsigned vmm_arena_find(void *context, unsigned pages, unsigned alignment, unsigned first, unsigned limit)
    {
        return static_cast<Arena *>(context)->Find(pages, alignment, first, limit);
    }
    void vmm_arena_release(void *context, std::uintptr_t block)
    {
        static_cast<Arena *>(context)->Release(reinterpret_cast<Block *>(block));
    }
    unsigned vmm_arena_page(std::uintptr_t block) { return reinterpret_cast<Block *>(block)->page; }
    unsigned vmm_arena_snapshot(void *context, unsigned allocated, unsigned *output)
    {
        const auto *arena = static_cast<Arena *>(context);
        const auto *head = allocated ? arena->AllocatedList() : arena->FreeList();
        unsigned count = 0;
        for (auto *block = head->next; block != head; block = block->next)
        {
            if (count >= 8)
                return ~0u;
            output[count * 3] = block->page;
            output[count * 3 + 1] = block->pages;
            output[count * 3 + 2] = block->flags;
            ++count;
        }
        return count;
    }
}
