// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_local_heap.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Heap final : Win98LocalHeap::Environment
{
    unsigned char memory[65536] = {};
    Heap() : Environment(memory) {}
    unsigned Compact(unsigned, unsigned &block) override
    {
        block = 0;
        return 0;
    }
    unsigned Notify(unsigned, unsigned, unsigned) override { return 0; }
    void Error(unsigned) override {}
    void Check() const
    {
        unsigned heap = W(6), head = W(heap + 6), tail = W(heap + 10), count = 0;
        unsigned previous = head, p = head;
        std::vector<unsigned> free;
        do
        {
            assert((p & 3) == 0 && p >= head && p <= tail);
            assert((W(p) & ~3u) == previous);
            if (!(W(p) & 1))
            {
                free.push_back(p);
                assert(W(p + 4) == W(p + 2) - p);
            }
            ++count;
            assert(count < 16384);
            if (p == tail)
                break;
            previous = p;
            p = W(p + 2);
            assert(p > previous);
        } while (true);
        assert(W(heap + 4) == count);
        p = head;
        previous = head;
        for (unsigned v : free)
        {
            assert(W(p + 8) == v);
            assert(W(v + 6) == p);
            p = v;
        }
        assert(W(p + 8) == tail && W(tail + 6) == p && W(tail + 8) == tail && W(head + 6) == head);
    }
};
int main()
{
    unsigned seed = 0x98a110c;
    auto random = [&]()
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    unsigned operations = 0;
    for (unsigned run = 0; run < 100; ++run)
    {
        Heap heap;
        assert(Win98LocalHeap::Initialize(heap, 0x1000, 0xfff0));
        std::vector<unsigned> live;
        for (unsigned i = 0; i < 2000; ++i)
        {
            if (!live.empty() && random() % 5 < 2)
            {
                unsigned at = random() % live.size();
                assert(!Win98LocalHeap::Free(heap, live[at]));
                live.erase(live.begin() + at);
            }
            else
            {
                unsigned flags =
                    (random() & 1 ? 2 : 0) | (random() & 1 ? 0x40 : 0) | (random() & 1 ? 0x10 : 0);
                unsigned result = Win98LocalHeap::Allocate(heap, flags, random() % 2048);
                if (result)
                    live.push_back(result);
            }
            heap.Check();
            ++operations;
        }
        for (unsigned p : live)
        {
            assert(!Win98LocalHeap::Free(heap, p));
            heap.Check();
            ++operations;
        }
    }
    std::printf("LOCAL HEAP ASAN/UBSAN PASS %u operations\n", operations);
}
