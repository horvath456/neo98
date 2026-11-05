// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_vmm_arena.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
using U = unsigned;
using Win98VmmArena::Block;
template <U Capacity> static void check(const Win98VmmArena::Arena<Capacity> &arena, U first, U limit)
{
    struct Range
    {
        U begin, end;
    };
    std::vector<Range> ranges;
    const auto *free = arena.FreeList();
    U last_end = 0;
    for (auto *b = free->next; b != free; b = b->next)
    {
        assert(ranges.size() < Capacity && b->pages && !b->flags);
        assert(b->page >= first && b->pages <= limit - b->page);
        assert(ranges.empty() || b->page > last_end); // Adjacent free blocks must already be merged.
        last_end = b->page + b->pages;
        ranges.push_back({b->page, last_end});
    }
    const auto *allocated = arena.AllocatedList();
    const auto *previous = allocated;
    for (auto *b = allocated->next; b != allocated; b = b->next)
    {
        assert(ranges.size() < Capacity && b->pages && b->previous == previous);
        assert(b->page >= first && b->pages <= limit - b->page);
        previous = b;
        ranges.push_back({b->page, b->page + b->pages});
    }
    assert(allocated->previous == previous);
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.begin < b.begin; });
    U at = first;
    for (auto range : ranges)
    {
        assert(range.begin == at);
        at = range.end;
    }
    assert(at == limit);
}
int main()
{
    Win98VmmArena::Arena<1> one;
    assert(!one.Reserve(16, 16)); // First prefix descriptor unavailable.
    assert(one.Available(0, 0x100000));
    auto *whole = one.Reserve(0, 0x100000);
    assert(whole && one.Find(1, 1, 0, 0x100000) == ~0u);
    one.Release(whole);
    check(one, 0, 0x100000);
    Win98VmmArena::Arena<2> two;
    for (U i = 0; i < 1000; ++i)
    {
        assert(!two.Reserve(16, 16)); // First split succeeds, second fails; rollback restores the interval.
        assert(two.Available(0, 0x100000));
        check(two, 0, 0x100000);
        auto *head = two.Reserve(0, 16, 3);
        assert(head && head->flags == 3);
        two.Release(head);
    }
    Win98VmmArena::Arena<3> three;
    for (U i = 0; i < 1000; ++i)
    {
        auto *middle = three.Reserve(16, 16, 3);
        assert(middle && !three.Available(16, 1));
        check(three, 0, 0x100000);
        auto *merged = three.Release(middle); // Successor and predecessor merge in one release.
        assert(merged->page == 0 && merged->pages == 0x100000 && !merged->flags);
    }
    Win98VmmArena::Arena<64> arena(0x400, 0x80000);
    std::vector<Block *> live;
    std::mt19937 random(0x989a0);
    for (U i = 0; i < 20000; ++i)
    {
        if (!live.empty() && (random() % 3 == 0 || live.size() >= 30))
        {
            const U index = random() % live.size();
            arena.Release(live[index]);
            live.erase(live.begin() + index);
        }
        else
        {
            const U pages = random() % 80 + 1, alignment = 1u << (random() % 7);
            const U page = arena.Find(pages, alignment, 0x400, 0x80000);
            if (page != ~0u)
            {
                assert(!(page & (alignment - 1)) && arena.Available(page, pages));
                if (auto *block = arena.Reserve(page, pages, 3))
                    live.push_back(block);
            }
        }
        check(arena, 0x400, 0x80000);
    }
    for (auto *block : live)
        arena.Release(block);
    check(arena, 0x400, 0x80000);
    assert(arena.Available(0x400, 0x7fc00));
    assert(arena.Find(0x7fc00, 16, 0x400, 0x80000) == 0x400);
    assert(!arena.Reserve(0x7ffff, 2) && !arena.Reserve(~0u, 1));
    puts(
        "VMM ARENA PASS original split/coalesce, both descriptor-failure paths, 20000 fragmented operations, "
        "alignment and complete address-space coverage");
}
