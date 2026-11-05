// SPDX-License-Identifier: GPL-2.0-or-later
// Isolated allocator failure injection for the native execution stack provider.
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <stdlib.h>
#include "../src/fiber.h"
static bool fail_allocation;
static unsigned attempts, allocations, releases, calls;
static void *fiber_malloc(size_t bytes)
{
    ++attempts;
    if (fail_allocation) return nullptr;
    void *p = malloc(bytes);
    if (p) ++allocations;
    return p;
}
static void fiber_free(void *p)
{
    if (p) ++releases;
    free(p);
}
#define malloc fiber_malloc
#define free fiber_free
#include "../src/fiber.cpp"
#undef free
#undef malloc
static void callback(void *p) { assert(p == &calls); ++calls; }
int main()
{
    for (unsigned size : {0u, 16383u, 0xfffffff1u, 0xffffffffu})
    {
        Fiber f;
        const unsigned before = attempts;
        assert(!f.Start(callback, &calls, size) && attempts == before);
    }
    {
        Fiber f;
        assert(!f.Start(nullptr, nullptr));
        fail_allocation = true;
        assert(!f.Start(callback, &calls, 65536));
        f.Resume();
        assert(!calls && !f.Finished());
        fail_allocation = false;
        assert(f.Start(callback, &calls, 65536));
        assert(!f.Start(callback, &calls, 65536));
        f.Resume();
        assert(calls == 1 && f.Finished());
        f.Resume();
        assert(calls == 1);
    }
    {
        Fiber suspended;
        assert(suspended.Start(callback, &calls, 65536));
    }
    assert(allocations == 2 && releases == allocations && attempts == 3);
    puts("FIBER ALLOC PASS: pre-allocation size checks, OOM retry, completion and suspended destruction");
}
