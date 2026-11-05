// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_atoms.h"
#include "../src/win98_local_heap.h"
#include <cstring>
#include <vector>
struct AtomHeap final : Win98LocalHeap::Environment
{
    unsigned char memory[65536] = {};
    std::vector<unsigned> events;
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
    struct Atoms final : Win98Atoms::Environment
    {
        AtomHeap &heap;
        Atoms(AtomHeap &h, const unsigned char *upper) : Environment(h.memory, upper), heap(h) {}
        unsigned Allocate(unsigned flags, unsigned size) override
        {
            return Win98LocalHeap::Allocate(heap, flags, size);
        }
        void Free(unsigned p) override { Win98LocalHeap::Free(heap, p); }
        bool Valid(unsigned p) const override
        {
            if (p == Word(8) || p == Word(6) || p < 0x1004 || (p & 3))
                return false;
            unsigned root = Word(6), block = Word(root + 6), tail = Word(root + 10);
            for (unsigned count = 0; count < 16384 && block < tail; ++count)
            {
                unsigned next = Word(block + 2);
                if (next <= block || next > tail)
                    return false;
                if (block + 4 == p)
                    return (Word(block) & 3) == 1 && Byte(p + 4) && Byte(p + 4) + 10 <= next - block;
                block = next;
            }
            return false;
        }
    };
    unsigned char mapping[128] = {};
    Atoms atoms;
    AtomHeap(const unsigned char *upper) : Environment(memory), atoms(*this, mapping)
    {
        std::memcpy(mapping, upper, 128);
        Win98LocalHeap::Initialize(*this, 0x1000, 0xb000);
    }
};
extern "C"
{
    void *atomheap_new(const unsigned char *upper) { return new AtomHeap(upper); }
    void atomheap_delete(AtomHeap *h) { delete h; }
    unsigned atomheap_search(AtomHeap *h, const unsigned char *s, unsigned mode)
    {
        return Win98Atoms::Search(h->atoms, s, mode);
    }
    unsigned atomheap_remove(AtomHeap *h, unsigned value) { return Win98Atoms::Delete(h->atoms, value); }
    unsigned atomheap_name(AtomHeap *h, unsigned value, unsigned char *out, unsigned size)
    {
        return Win98Atoms::Name(h->atoms, value, out, size);
    }
    unsigned char *atomheap_data(AtomHeap *h) { return h->memory; }
    const unsigned *atomheap_events(AtomHeap *h, unsigned *count)
    {
        *count = h->events.size();
        return h->events.data();
    }
}
