// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/fiber.h"
#include <cassert>
#include <cstdio>
struct Job
{
    Fiber fiber;
    unsigned seed, visits = 0;
};
static void nested(Job &job, unsigned depth)
{
    volatile unsigned locals[257];
    for (unsigned i = 0; i < 257; ++i)
        locals[i] = i + job.seed + depth;
    if (depth)
        nested(job, depth - 1);
    for (unsigned step = 0; step < 30; ++step)
    {
        ++job.visits;
        job.fiber.Yield();
        for (unsigned i = 0; i < 257; ++i)
            assert(locals[i] == i + job.seed + depth);
    }
}
int main()
{
    Job a{}, b{};
    a.seed = 98;
    b.seed = 0x12345678;
    auto entry = [](void *p) { nested(*static_cast<Job *>(p), 12); };
    assert(a.fiber.Start(entry, &a));
    assert(b.fiber.Start(entry, &b));
    while (!a.fiber.Finished() || !b.fiber.Finished())
    {
        a.fiber.Resume();
        b.fiber.Resume();
    }
    assert(a.visits == 390 && b.visits == 390);
    puts("FIBER PASS: 780 interleaved yields, nested stacks preserved");
}
