// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_atoms.h"
#include <cassert>
#include <cstring>
#include <vector>
struct TestAtoms final : Win98Atoms::Environment
{
    unsigned char data[65536] = {}, mapping[128];
    unsigned lengths[16384] = {}, next = 0x1000, fail = 0;
    std::vector<unsigned> events;
    explicit TestAtoms(const unsigned char *upper) : Environment(data, mapping)
    {
        std::memcpy(mapping, upper, 128);
    }
    unsigned Allocate(unsigned flags, unsigned size) override
    {
        unsigned p = next + 4;
        if (fail || p > 0xb000 || size > 0xb000 - p)
        {
            fail = 0;
            p = 0;
        }
        events.insert(events.end(), {0, flags, size, p});
        if (p)
        {
            next = (p + size + 3) & ~3u;
            lengths[p / 4] = size;
            Word(p - 4, (p - 4) | 1);
            Word(p - 2, p - 4);
            std::memset(data + p, 0, size);
        }
        return p;
    }
    void Free(unsigned p) override
    {
        assert(p % 4 == 0 && lengths[p / 4]);
        lengths[p / 4] = 0;
        Word(p - 4, 0);
        Word(p - 2, 0);
        events.insert(events.end(), {1, p, 0, 0});
    }
    bool Valid(unsigned p) const override
    {
        return p && !(p & 3) && p < 65536 && lengths[p / 4] && p != Word(8);
    }
};
extern "C" void *atoms_new(const unsigned char *upper) { return new TestAtoms(upper); }
extern "C" void atoms_destroy(TestAtoms *p) { delete p; }
extern "C" unsigned atoms_init(TestAtoms *p, unsigned buckets) { return Win98Atoms::Initialize(*p, buckets); }
extern "C" unsigned atoms_search(TestAtoms *p, const unsigned char *name, unsigned operation)
{
    return Win98Atoms::Search(*p, name, operation);
}
extern "C" unsigned atoms_integer(unsigned value) { return Win98Atoms::Integer(value); }
extern "C" unsigned atoms_delete(TestAtoms *p, unsigned atom) { return Win98Atoms::Delete(*p, atom); }
extern "C" unsigned atoms_name(TestAtoms *p, unsigned atom, unsigned char *out, unsigned capacity)
{
    return Win98Atoms::Name(*p, atom, out, capacity);
}
extern "C" void atoms_fail(TestAtoms *p) { p->fail = 1; }
extern "C" void atoms_set_word(TestAtoms *p, unsigned at, unsigned word) { p->Word(at, word); }
extern "C" const unsigned char *atoms_memory(TestAtoms *p) { return p->data; }
extern "C" const unsigned *atoms_events(TestAtoms *p, unsigned *count)
{
    *count = p->events.size();
    return p->events.data();
}
