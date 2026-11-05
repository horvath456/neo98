// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_local_heap.h"
#include <cstring>
#include <vector>
struct Heap final : Win98LocalHeap::Environment
{
    unsigned char memory[65536] = {};
    std::vector<unsigned> events;
    Heap() : Environment(memory) {}
    unsigned Compact(unsigned requested, unsigned &block) override
    {
        events.insert(events.end(), {1, requested, 0});
        block = 0;
        return 0;
    }
    unsigned Notify(unsigned reason, unsigned handle, unsigned size) override
    {
        events.insert(events.end(), {2, reason, handle, size});
        return 0;
    }
    void Error(unsigned code) override { events.insert(events.end(), {3, code}); }
};
extern "C"
{
    void *heap_new() { return new Heap; }
    void heap_delete(Heap *h) { delete h; }
    unsigned heap_init(Heap *h, unsigned start, unsigned end)
    {
        return Win98LocalHeap::Initialize(*h, start, end);
    }
    unsigned heap_alloc(Heap *h, unsigned flags, unsigned size)
    {
        return Win98LocalHeap::Allocate(*h, flags, size);
    }
    unsigned heap_realloc(Heap *h, unsigned value, unsigned size, unsigned flags)
    {
        return Win98LocalHeap::ReAllocate(*h, value, size, flags);
    }
    unsigned heap_lock(Heap *h, unsigned value) { return Win98LocalHeap::Lock(*h, value); }
    unsigned heap_unlock(Heap *h, unsigned value) { return Win98LocalHeap::Unlock(*h, value); }
    unsigned heap_flags(Heap *h, unsigned value) { return Win98LocalHeap::Flags(*h, value); }
    unsigned heap_handle(Heap *h, unsigned value) { return Win98LocalHeap::Handle(*h, value); }
    unsigned heap_free(Heap *h, unsigned value) { return Win98LocalHeap::Free(*h, value); }
    unsigned char *heap_data(Heap *h) { return h->memory; }
    const unsigned *heap_events(Heap *h, unsigned *count)
    {
        *count = h->events.size();
        return h->events.data();
    }
    void heap_word(Heap *h, unsigned offset, unsigned value) { h->W(offset, value); }
}
