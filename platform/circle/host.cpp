// SPDX-License-Identifier: GPL-2.0-or-later
#include <circle/alloc.h>
#include <circle/memory.h>
#include <new>
#include <circle/timer.h>
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <assert.h>

extern "C" {
unsigned long long neo98_clock_us() { return CTimer::GetClockTicks64(); }
void neo98_diagnostic(const char *message) { CLogger::Get()->Write("unicorn", LogError, "%s", message); }
void __clear_cache(void *start, void *end)
{
    // Clean generated instructions to PoU, then invalidate the I-cache.
    unsigned long ctr;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    unsigned long dline = 4ul << ((ctr >> 16) & 15), iline = 4ul << (ctr & 15);
    for (unsigned long p = (unsigned long)start & ~(dline - 1); p < (unsigned long)end; p += dline)
        asm volatile("dc cvau, %0" :: "r"(p) : "memory");
    asm volatile("dsb ish" ::: "memory");
    for (unsigned long p = (unsigned long)start & ~(iline - 1); p < (unsigned long)end; p += iline)
        asm volatile("ic ivau, %0" :: "r"(p) : "memory");
    asm volatile("dsb ish; isb" ::: "memory");
}
}

// The nothrow forms of platform/circle/include/new. Circle's heap returns
// null when it is exhausted; the plain forms in Circle's lib/new.cpp use the
// same heap, so delete releases both.
namespace std { const nothrow_t nothrow{}; }
void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    return CMemorySystem::HeapAllocate(size, HEAP_DEFAULT_NEW);
}
void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    return CMemorySystem::HeapAllocate(size, HEAP_DEFAULT_NEW);
}
void operator delete(void *block, const std::nothrow_t &) noexcept { CMemorySystem::HeapFree(block); }
void operator delete[](void *block, const std::nothrow_t &) noexcept { CMemorySystem::HeapFree(block); }
