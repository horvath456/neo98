// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/virtual_memory.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
using U = unsigned;
struct Backend final : VirtualMemory::Backend
{
    struct Page
    {
        std::array<unsigned char, 4096> bytes{};
        U permissions = 0;
    };
    std::map<U, Page> pages;
    U maps = 0, fail_at = 0, availability_calls = 0, unmaps = 0, fail_unmap_at = 0;
    U protects = 0, fail_protect_at = 0;
    bool reject_address = false;
    bool Available(U, U) override
    {
        ++availability_calls;
        return !reject_address;
    }
    bool Map(U base, U size, U permissions) override
    {
        if (++maps == fail_at)
            return false;
        for (U p = base; p < base + size; p += 4096)
            assert(!pages.count(p));
        for (U p = base; p < base + size; p += 4096)
            pages[p].permissions = permissions;
        return true;
    }
    bool Unmap(U base, U size) override
    {
        if (++unmaps == fail_unmap_at)
            return false;
        for (U p = base; p < base + size; p += 4096)
            assert(pages.erase(p) == 1);
        return true;
    }
    bool Protect(U base, U size, U flags) override
    {
        if (++protects == fail_protect_at)
            return false;
        for (U p = base; p < base + size; p += 4096)
        {
            assert(pages.count(p));
            pages[p].permissions = flags;
        }
        return true;
    }
};
static void interval_integration()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    assert(memory.Exclude(0x400000, 0x3fc00000));
    assert(memory.Exclude(0x400000, 0x3fc00000)); // Reapplying the same platform reservation is harmless.
    assert(!memory.Available(0x400000, 4096));
    assert(memory.Query(0x400000, info) && info.allocation == 0x400000 && info.state == 0x2000);
    assert(memory.Track(0x500000, 4096, 4));
    assert(memory.Access(0x500000, 4096, true));
    memory.Forget(0x500000, 4096);
    assert(!memory.Available(0x500000, 4096)); // Native unmapping cannot return the permanent exclusion.
    assert(!memory.Track(0x3ff000, 8192, 4));
    assert(!memory.Track(0x3ffff000, 8192, 4));
    const U after_hole = memory.Reserve(e, 0x80000400, 1, 0);
    assert(after_hole == 0x40000000 && e.availability_calls == 1); // One free-interval selection.
    assert(memory.Release(e, after_hole));
    e.reject_address = true;
    assert(memory.Reserve(e, 0x80000400, 1, 0) == ~0u);
    assert(memory.Available(0x40000000, 65536));
    e.reject_address = false;
    assert(memory.Track(0x40000000, 4096, 4));
    assert(memory.Track(0x40001000, 4096, 4));
    memory.Forget(0x40000000, 4096);
    assert(memory.Available(0x40000000, 4096) && !memory.Available(0x40000000, 8192));
    memory.Forget(0x40001000, 4096);
    assert(memory.Available(0x40000000, 65536));
    assert(memory.Reserve(e, 0x80000400, 1, 0) == 0x40000000);
    assert(memory.Release(e, 0x40000000));
    const U shared1 = memory.Reserve(e, 0x80060000, 1, 0);
    const U shared2 = memory.Reserve(e, 0x80060000, 1, 0);
    assert(shared1 == 0x90000000 && shared2 == shared1 + 4096);
    assert(memory.Release(e, shared1) && memory.Release(e, shared2));
    // High original DLL mappings and the last guest page remain trackable.
    assert(memory.Track(0xbff70000, 0x10000, 2));
    assert(memory.Track(0xfffff000, 4096, 2));
    assert(!memory.Available(0xfffff000, 4096) && memory.Access(0xffffffff, 1, false));
    memory.Forget(0xfffff000, 4096);
    memory.Forget(0xbff70000, 0x10000);
    assert(memory.Available(0xfffff000, 4096));
    assert(!memory.Available(0xffffffff, 2));
}
static void descriptor_lifecycle()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    constexpr U origin = 0x1000000;
    for (U i = 0; i < 16384; ++i)
        assert(memory.Track(origin + i * 8192, 4096, 4));
    assert(!memory.Track(0x20000000, 4096, 4));
    assert(memory.Reserve(e, 0x80000400, 1, 0) == ~0u);
    assert(memory.Access(origin + 16383 * 8192, 4096, true));
    // Releasing the highest descriptor must shrink traversal without losing its predecessor.
    memory.Forget(origin + 16383 * 8192, 4096);
    assert(!memory.Access(origin + 16383 * 8192, 1, false));
    assert(memory.Access(origin + 16382 * 8192, 4096, true));
    const U owned = memory.Reserve(e, 0x80000400, 1, 0x60000);
    assert(owned != ~0u && memory.Commit(e, owned >> 12, 1, 0x60000));
    assert(memory.Query(owned, info) && info.state == 0x1000);
    memory.Forget(owned, 65536); // Forget cannot release a VirtualAlloc-owned descriptor.
    assert(memory.Access(owned, 4096, true));
    assert(!memory.Track(0x20000000, 4096, 4));
    assert(memory.Release(e, owned));
    assert(memory.Track(origin + 16383 * 8192, 4096, 4));
    // Exercise holes on both sides of bitmap word boundaries, and reuse every released slot.
    constexpr U holes[] = {0, 31, 32, 63, 127, 16382, 16383};
    for (U i : holes)
        memory.Forget(origin + i * 8192, 4096);
    for (U i : holes)
    {
        assert(memory.Track(0x20000000 + i * 8192, 4096, 2));
        assert(memory.Access(0x20000000 + i * 8192, 4096, false));
        assert(!memory.Access(0x20000000 + i * 8192, 1, true));
    }
    assert(!memory.Track(0x30000000, 4096, 4));
    for (U i = 0; i < 16384; ++i)
        memory.Forget(origin + i * 8192, 4096);
    for (U i : holes)
        memory.Forget(0x20000000 + i * 8192, 4096);
    assert(memory.Query(0, info) && info.state == 0x10000 && info.size == 0xc0000000);
    assert(!memory.Overlaps(origin, 0x20000000) && e.pages.empty());
    assert(memory.Track(origin, 4096, 4)); // Fully exhausted and then empty is reusable.
}
static void reservation_granularity()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    const U base = memory.Reserve(e, 0x80000400, 1, 0x60000);
    assert(base == 0x400000 && memory.Query(base, info) && info.size == 65536);
    assert(memory.Commit(e, (base >> 12) + 15, 1, 0x60000));
    assert(!memory.Commit(e, (base >> 12) + 16, 1, 0x60000));
    assert(memory.Release(e, base));
    const U two = memory.Reserve(e, 0x500f, 2, 0x60000);
    assert(two == 0x5000000 && memory.Query(two, info) && info.size == 131072);
    assert(memory.Overlaps(two + 131071, 1) && !memory.Overlaps(two + 131072, 1));
    assert(memory.Release(e, two));
    const U automatic = memory.Reserve(e, 0x80000400, 17, 0);
    assert(automatic == base && memory.Query(automatic, info) && info.size == 131072);
    assert(memory.Release(e, automatic));
    const U last_private = memory.Reserve(e, 0x7ffff, 1, 0);
    assert(last_private == 0x7fff0000 && memory.Query(last_private, info) && info.size == 65536);
    assert(memory.Release(e, last_private));
    assert(memory.Reserve(e, 0x7ffff, 2, 0) == ~0u);
    // The original VMM bypasses private-arena rounding for shared addresses.
    const U shared = memory.Reserve(e, 0x80001, 1, 0);
    assert(shared == 0x80001000 && memory.Query(shared, info) && info.size == 4096);
    assert(memory.Release(e, shared));
    const U automatic_shared = memory.Reserve(e, 0x80060000, 1, 0);
    assert(automatic_shared == 0x90000000 && memory.Query(automatic_shared, info) && info.size == 4096);
    assert(memory.Release(e, automatic_shared) && e.pages.empty());
}
static void executable_pages()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    constexpr U base = 0x400000;
    assert(!memory.Executable(base));
    assert(memory.Track(base, 8192, 4));
    assert(memory.Access(base, 1, false) && !memory.Executable(base));
    assert(memory.SetProtection(base, 4096, 0x10));
    assert(memory.Executable(base) && memory.Executable(base + 4095));
    assert(!memory.Access(base, 1, false) && !memory.Access(base, 1, true));
    assert(!memory.Executable(base + 4096));
    assert(memory.SetProtection(base, 4096, 1) && !memory.Executable(base));
    memory.Forget(base, 8192);
    assert(!memory.Executable(base));
    assert(memory.Track(0xfffff000, 4096, 0x10));
    assert(memory.Executable(~0u) && !memory.Executable(0));
    memory.Forget(0xfffff000, 4096);
    const U reserved = memory.Reserve(e, 0x80000400, 1, 0x40000);
    assert(reserved != ~0u && !memory.Executable(reserved));
    assert(memory.Commit(e, reserved >> 12, 1, 0x40000) && memory.Executable(reserved));
    assert(memory.Decommit(e, reserved >> 12, 1) && !memory.Executable(reserved));
    assert(memory.Release(e, reserved));
}
static void mapping_ownership()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    // Address selection and descriptor allocation are shared, ownership is not.
    const U ordinary = memory.Reserve(e, 0x80060000, 1, 0x60000);
    const U mapped = memory.ReserveMapping(e, 0x80060000, 3, 0x60000);
    assert(ordinary == 0x90000000 && mapped == ordinary + 4096);
    assert(memory.Track(mapped + 3 * 4096, 4096, 2));
    assert(memory.Query(mapped, info) && info.type == 0x40000 && info.state == 0x2000 &&
           info.allocation == mapped && info.allocation_protection == 4 && info.size == 3 * 4096);
    assert(memory.Query(ordinary, info) && info.type == 0x20000);
    assert(!memory.CommitMapping(e, ordinary >> 12, 1, 0x60000));
    assert(!memory.ReleaseMapping(e, ordinary));
    assert(!memory.CommitMapping(e, (mapped >> 12) + 3, 1, 0x60000));
    assert(!memory.ReleaseMapping(e, mapped + 3 * 4096));
    assert(!memory.Commit(e, mapped >> 12, 1, 0x60000));
    assert(!memory.Decommit(e, mapped >> 12, 1) && !memory.Release(e, mapped));
    memory.Forget(mapped, 3 * 4096);
    assert(memory.Query(mapped, info) && info.type == 0x40000 && e.pages.empty());
    assert(memory.Commit(e, ordinary >> 12, 1, 0x60000));
    assert(memory.CommitMapping(e, mapped >> 12, 1, 0x40000));
    assert(memory.CommitMapping(e, (mapped >> 12) + 2, 1, 0x60000));
    e.pages[mapped].bytes[98] = 98;
    e.pages[mapped + 8192].bytes[98] = 97;
    assert(memory.Query(mapped, info) && info.type == 0x40000 && info.state == 0x1000 &&
           info.protection == 2 && info.size == 4096);
    assert(memory.Query(mapped + 4096, info) && info.state == 0x2000 && info.type == 0x40000 &&
           info.allocation == mapped && info.size == 4096);
    // An overlapping WRITE view preserves bytes and adds the original VMM write bit.
    const U maps = e.maps;
    assert(memory.CommitMapping(e, mapped >> 12, 3, 0x60000) && e.maps == maps + 1);
    assert(e.pages[mapped].bytes[98] == 98 && e.pages[mapped].permissions == 7);
    assert(e.pages[mapped + 4096].bytes[98] == 0 && e.pages[mapped + 4096].permissions == 7);
    assert(e.pages[mapped + 8192].bytes[98] == 97 && memory.CommittedPages() == 4);
    assert(memory.CommitMapping(e, mapped >> 12, 3, 0) && e.maps == maps + 1);
    assert(memory.Access(mapped, 3 * 4096, false) && memory.Access(mapped, 1, true));
    assert(memory.Executable(mapped) && memory.Access(mapped + 4096, 2 * 4096, true));
    assert(!memory.Commit(e, mapped >> 12, 3, 0));
    assert(!memory.Decommit(e, mapped >> 12, 3) && !memory.Release(e, mapped));
    assert(memory.CommittedPages() == 4 && e.pages[mapped].bytes[98] == 98);
    assert(memory.Protect(e, mapped + 4095, 2, 0) == 0x60000);
    assert(!memory.Access(mapped, 1, false) && !memory.Executable(mapped));
    assert(!memory.Access(mapped + 4096, 1, true) && memory.Access(mapped + 8192, 1, true));
    assert(memory.Query(mapped, info) && info.type == 0x40000 && info.protection == 1 &&
           info.size == 8192 && info.allocation_protection == 4);
    assert(memory.Protect(e, mapped, 8192, 0x40000) == 0);
    assert(memory.Access(mapped, 8192, false) && !memory.Access(mapped, 1, true));
    assert(!memory.ReleaseMapping(e, mapped + 1) && !memory.ReleaseMapping(e, mapped + 4096));
    assert(memory.ReleaseMapping(e, mapped) && memory.CommittedPages() == 1);
    assert(memory.Query(mapped, info) && info.state == 0x10000 && !info.type);
    assert(memory.ReserveMapping(e, mapped >> 12, 3, 0x60000) == mapped);
    assert(memory.CommitMapping(e, mapped >> 12, 1, 0x60000) && !e.pages[mapped].bytes[98]);
    assert(memory.ReleaseMapping(e, mapped) && memory.Release(e, ordinary));
    memory.Forget(mapped + 3 * 4096, 4096);
    assert(e.pages.empty() && !memory.CommittedPages());
    // Removing all three owner kinds coalesces the same original free-block list.
    assert(memory.ReserveMapping(e, 0x80060000, 5, 0x40000) == ordinary);
    assert(memory.Query(ordinary, info) && info.size == 5 * 4096 && info.type == 0x40000);
    assert(memory.ReleaseMapping(e, ordinary));
}
static void mapping_rollback()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    assert(memory.Exclude(0x90000000, 4096));
    e.reject_address = true;
    assert(memory.ReserveMapping(e, 0x80060000, 5, 0x60000) == ~0u);
    assert(memory.Available(0x90001000, 5 * 4096));
    e.reject_address = false;
    const U base = memory.ReserveMapping(e, 0x80060000, 5, 0x60000);
    assert(base == 0x90001000 && memory.CommittedPages() == 0);
    assert(memory.ReserveMapping(e, base >> 12, 1, 0x60000) == ~0u);
    assert(memory.CommitMapping(e, (base >> 12) + 1, 1, 0x60000));
    assert(memory.CommitMapping(e, (base >> 12) + 3, 1, 0x60000));
    e.pages[base + 4096].bytes[98] = 98;
    e.pages[base + 12288].bytes[98] = 97;
    for (U run = 1; run <= 3; ++run)
    {
        e.fail_at = e.maps + run;
        assert(!memory.CommitMapping(e, base >> 12, 5, 0x40000));
        assert(memory.CommittedPages() == 2 && e.pages.size() == 2);
        assert(e.pages[base + 4096].bytes[98] == 98 && e.pages[base + 12288].bytes[98] == 97);
        for (U page = 0; page < 5; ++page)
            assert(memory.Query(base + page * 4096, info) && info.size == 4096 &&
                   info.type == 0x40000 && info.state == (page & 1 ? 0x1000u : 0x2000u));
    }
    e.fail_at = 0;
    // A failed release retains the reservation and accurate state for its retry.
    e.fail_unmap_at = e.unmaps + 2;
    assert(!memory.ReleaseMapping(e, base));
    assert(memory.CommittedPages() == 1 && e.pages.size() == 1);
    assert(memory.Query(base, info) && info.state == 0x2000 && info.size == 3 * 4096 &&
           info.type == 0x40000 && info.allocation == base);
    assert(e.pages[base + 12288].bytes[98] == 97 && !memory.Release(e, base));
    e.fail_unmap_at = 0;
    assert(memory.ReleaseMapping(e, base) && e.pages.empty() && !memory.CommittedPages());
    assert(memory.ReserveMapping(e, 0x80060000, 5, 0x60000) == base);
    assert(!memory.CommitMapping(e, base >> 12, 6, 0x60000));
    assert(!memory.CommitMapping(e, (base >> 12) - 1, 2, 0x60000));
    assert(memory.Protect(e, base, 4096, 0x40000) == ~0u);
    const U calls = e.availability_calls;
    for (U flags : {1u, 0x80000u, 0x260000u})
    {
        assert(memory.ReserveMapping(e, 0x80060000, 1, flags) == ~0u);
        assert(!memory.CommitMapping(e, base >> 12, 1, flags));
    }
    assert(memory.ReserveMapping(e, 0x80060000, 0, 0) == ~0u);
    assert(memory.ReserveMapping(e, 0xbfbff, 2, 0) == ~0u);
    assert(memory.ReserveMapping(e, 0xbfc00, 1, 0) == ~0u);
    assert(e.availability_calls == calls && !memory.Available(0x90000000, 4096));
    assert(e.pages.empty() && !memory.CommittedPages() && memory.ReleaseMapping(e, base));
}
static void mapping_protection_rollback()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    const U base = memory.ReserveMapping(e, 0x80060000, 5, 0x60000);
    assert(base != ~0u);
    for (U i : {0u, 2u, 4u})
    {
        assert(memory.CommitMapping(e, (base >> 12) + i, 1, 0x40000));
        e.pages[base + i * 4096].bytes[98] = 98 + i;
    }
    assert(memory.Protect(e, base + 8192, 4096, 0) == 0x40000);
    // Fail each upgrade in turn. New pages are removed, while old bytes and
    // permissions return to their pre-call values, including a no-access page.
    for (U failure = 1; failure <= 3; ++failure)
    {
        e.fail_protect_at = e.protects + failure;
        assert(!memory.CommitMapping(e, base >> 12, 5, 0x60000));
        assert(memory.CommittedPages() == 3 && e.pages.size() == 3);
        for (U i : {0u, 2u, 4u})
        {
            assert(e.pages[base + i * 4096].bytes[98] == 98 + i);
            assert(e.pages[base + i * 4096].permissions == (i == 2 ? 0u : 5u));
            assert(!memory.Access(base + i * 4096, 1, true));
        }
        assert(!e.pages.count(base + 4096) && !e.pages.count(base + 12288));
    }
    e.fail_protect_at = 0;
    assert(memory.CommitMapping(e, base >> 12, 5, 0x60000));
    assert(memory.Access(base, 5 * 4096, true) && memory.CommittedPages() == 5);
    assert(memory.CommitMapping(e, base >> 12, 5, 0x40000));
    assert(memory.Access(base, 5 * 4096, true)); // A READ view does not revoke WRITE.
    assert(e.pages[base].bytes[98] == 98 && e.pages[base + 16384].bytes[98] == 102);
    assert(memory.ReleaseMapping(e, base) && e.pages.empty());
}
static void mapping_descriptors()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    for (U i = 0; i < 16384; ++i)
        assert(memory.ReserveMapping(e, 0x80060000, 1, 0x60000) == 0x90000000 + i * 4096);
    assert(memory.ReserveMapping(e, 0x80060000, 1, 0x60000) == ~0u);
    assert(memory.Reserve(e, 0x80000400, 1, 0x60000) == ~0u);
    for (U i = 0; i < 16384; i += 2)
        assert(memory.ReleaseMapping(e, 0x90000000 + i * 4096));
    for (U i = 0; i < 16384; i += 2)
        assert(memory.ReserveMapping(e, 0x80060000, 1, 0x40000) == 0x90000000 + i * 4096);
    for (U i = 0; i < 16384; ++i)
    {
        const U base = 0x90000000 + i * 4096;
        assert(memory.Query(base, info) && info.type == 0x40000 && info.allocation == base &&
               info.allocation_protection == (i & 1 ? 4u : 2u));
        assert(memory.ReleaseMapping(e, base));
    }
    assert(memory.Available(0x90000000, 16384 * 4096));
}
static void mapping_commit_quota()
{
    // Test the full metadata quota without allocating 256 MiB of test byte arrays.
    struct CountingBackend final : VirtualMemory::Backend
    {
        U pages = 0, maps = 0;
        bool Available(U, U) override { return true; }
        bool Map(U, U size, U) override { pages += size >> 12; ++maps; return true; }
        bool Unmap(U, U size) override
        {
            assert(pages >= size >> 12);
            pages -= size >> 12;
            return true;
        }
        bool Protect(U, U, U) override { return true; }
    } e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    const U mapped = memory.ReserveMapping(e, 0x80060000, 65536, 0x60000);
    const U ordinary = memory.Reserve(e, 0x80000400, 1, 0x60000);
    assert(mapped == 0x90000000 && ordinary == 0x400000);
    assert(memory.CommitMapping(e, mapped >> 12, 65535, 0x60000));
    assert(memory.Commit(e, ordinary >> 12, 1, 0x60000));
    assert(memory.CommittedPages() == 65536 && e.pages == 65536 && e.maps == 2);
    assert(!memory.CommitMapping(e, (mapped >> 12) + 65535, 1, 0x60000) && e.maps == 2);
    assert(memory.CommitMapping(e, mapped >> 12, 65535, 0x40000) && e.maps == 2);
    assert(memory.Release(e, ordinary) && memory.CommittedPages() == 65535);
    assert(memory.CommitMapping(e, (mapped >> 12) + 65535, 1, 0x60000));
    assert(memory.CommittedPages() == 65536 && e.pages == 65536 && e.maps == 3);
    assert(memory.Reserve(e, 0x80000400, 1, 0x60000) == ordinary);
    assert(!memory.Commit(e, ordinary >> 12, 1, 0x60000) && e.maps == 3);
    assert(memory.ReleaseMapping(e, mapped) && !memory.CommittedPages() && !e.pages);
    assert(memory.Commit(e, ordinary >> 12, 1, 0x60000));
    assert(memory.Release(e, ordinary) && !memory.CommittedPages() && !e.pages);
}
static void heap_ownership()
{
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    const U root = memory.ReserveHeap(e, 0x80000400, 1);
    const U shared = memory.ReserveHeap(e, 0x80060000, 1024);
    assert(root == 0x400000 && shared == 0x90000000);
    assert(memory.Query(root, info) && info.size == 65536 && info.type == 0x20000);
    assert(memory.CommitHeap(e, root >> 12, 1));
    assert(memory.CommitHeap(e, shared >> 12, 2));
    e.pages[shared].bytes[98] = 98;
    assert(memory.CommitHeap(e, shared >> 12, 2) && e.pages[shared].bytes[98] == 98);
    assert(memory.Query(shared, info) && info.type == 0x20000 && info.state == 0x1000);
    assert(!memory.Release(e, root) && !memory.ReleaseMapping(e, shared));
    assert(!memory.Decommit(e, root >> 12, 1) && !memory.CommitMapping(e, shared >> 12, 1, 0x60000));
    assert(!memory.Commit(e, shared >> 12, 1, 0x60000));
    memory.Forget(root, 65536);
    assert(memory.Access(root, 4096, true));
    assert(memory.DecommitHeap(e, shared >> 12, 1));
    assert(memory.CommitHeap(e, shared >> 12, 1) && !e.pages[shared].bytes[98]);
    assert(memory.ReleaseHeap(e, shared) && memory.ReleaseHeap(e, root));
    assert(!memory.CommittedPages() && e.pages.empty());
    const U ordinary = memory.Reserve(e, 0x80000400, 1, 0x60000);
    const U mapped = memory.ReserveMapping(e, 0x80060000, 1, 0x60000);
    assert(!memory.ReleaseHeap(e, ordinary) && !memory.ReleaseHeap(e, mapped));
    assert(!memory.CommitHeap(e, ordinary >> 12, 1) && !memory.DecommitHeap(e, mapped >> 12, 1));
    assert(memory.Release(e, ordinary) && memory.ReleaseMapping(e, mapped));
}
int main()
{
    heap_ownership();
    interval_integration();
    descriptor_lifecycle();
    reservation_granularity();
    executable_pages();
    mapping_ownership();
    mapping_rollback();
    mapping_descriptors();
    mapping_protection_rollback();
    mapping_commit_quota();
    Backend e;
    // Keep the large descriptor store off the test stack when cases inline.
    auto owned_memory = std::make_unique<VirtualMemory::Space>();
    auto &memory = *owned_memory;
    VirtualMemory::Info info{};
    assert(memory.Track(0x400000, 0x10000, 0x20));
    assert(memory.Query(0x400000, info) && info.protection == 2 && info.allocation_protection == 2);
    const U base = memory.Reserve(e, 0x80000400, 65536, 0x60000); // 256 MiB reservation, zero backing.
    assert(base == 0x410000 && e.pages.empty() && !memory.CommittedPages());
    assert(memory.Query(base + 123, info) && info.base == base && info.allocation == base &&
           info.size == 0x10000000 && info.state == 0x2000 && info.protection == 0);
    assert(!memory.Access(base, 1, false));
    assert(memory.Commit(e, (base >> 12) + 1, 1, 0x60000));
    e.pages[base + 4096].bytes[98] = 98;
    assert(memory.Commit(e, (base >> 12) + 3, 1, 0x60000));
    e.pages[base + 12288].bytes[98] = 97;
    // A multi-run commit fails after one new run. Only new pages are rolled back.
    e.fail_at = e.maps + 2;
    assert(!memory.Commit(e, base >> 12, 5, 0x60000));
    assert(memory.CommittedPages() == 2 && e.pages.size() == 2);
    assert(e.pages[base + 4096].bytes[98] == 98 && e.pages[base + 12288].bytes[98] == 97);
    assert(memory.Query(base, info) && info.size == 4096 && info.state == 0x2000);
    e.fail_at = 0;
    assert(memory.Commit(e, base >> 12, 5, 0x40000));
    assert(memory.CommittedPages() == 5 && e.pages.size() == 5);
    assert(e.pages[base].bytes[98] == 0 && e.pages[base].permissions == 5);
    assert(e.pages[base + 4096].bytes[98] == 98 && e.pages[base + 4096].permissions == 7);
    assert(!memory.Access(base, 1, true) && memory.Access(base, 1, false));
    assert(memory.Protect(e, base + 4095, 2, 0) == 0x40000);
    assert(!memory.Access(base, 1, false) && !memory.Access(base + 4096, 1, true));
    assert(memory.Protect(e, base + 4096, 4096, 0x60000) == 0);
    assert(memory.Access(base + 4096, 4096, true) && !memory.Access(base + 8191, 2, true));
    assert(memory.Protect(e, base + 16384, 8192, 0x60000) == ~0u);
    assert(memory.Query(base + 16384, info) && info.state == 0x1000 && info.protection == 2);
    assert(memory.Decommit(e, (base >> 12) + 1, 2) && memory.CommittedPages() == 3);
    assert(memory.Decommit(e, (base >> 12) + 1, 2) && memory.CommittedPages() == 3);
    assert(memory.Commit(e, (base >> 12) + 1, 1, 0x60000) && e.pages[base + 4096].bytes[98] == 0);
    assert(!memory.Release(e, base + 4096) && memory.Release(e, base));
    assert(e.pages.empty() && !memory.CommittedPages());
    assert(!memory.Release(e, 0x400000)); // Loader-owned ranges cannot be freed by VirtualFree.
    for (U i = 0; i < 300; ++i)
    {
        U p = memory.Reserve(e, 0x80000400, 1, 0);
        assert(p == base && memory.Commit(e, p >> 12, 1, 0));
        assert(e.pages[p].permissions == 0 && memory.Release(e, p));
    }
    U p = memory.Reserve(e, 0x5001, 2, 0x60000);
    assert(p == 0x5000000 && memory.Query(p, info) && info.size == 65536);
    assert(memory.Reserve(e, 0x5002, 1, 0x60000) == ~0u && memory.Release(e, p));
    assert(memory.Reserve(e, 0x80000400, 0, 0) == ~0u);
    assert(memory.Reserve(e, 0x80000400, 1, 0x200000) == ~0u);
    assert(!memory.Commit(e, 0xfffff, 2, 0x60000));
    assert(!memory.Access(~0u, 2, false) && memory.Access(~0u, 0, false));
    memory.Forget(0x400000, 0x10000);
    assert(!memory.Overlaps(0x400000, 0x10000));
    // An execute-only loader mapping still reports the legacy read/execute VMM bit.
    assert(memory.Track(0x400000, 4096, 0x10) && e.Map(0x400000, 4096, 4));
    assert(memory.Query(0x400000, info) && info.protection == 2);
    assert(memory.Protect(e, 0x400000, 4096, 0x60000) == 0x40000);
    assert(memory.Access(0x400000, 4096, true) && memory.Executable(0x400000));
    assert(e.Unmap(0x400000, 4096));
    memory.Forget(0x400000, 4096);
    // an image reservation keeps gaps distinct from committed NOACCESS.
    assert(memory.Track(0x400000, 0x30000, 4));
    assert(memory.SetProtection(0x400000, 0x30000, 0));
    assert(memory.SetProtection(0x400000, 0x1000, 2));
    assert(memory.SetProtection(0x410000, 0x1000, 2));
    assert(memory.SetProtection(0x420000, 0x2000, 4));
    for (U gap : {0x401000u, 0x411000u, 0x422000u}) {
        assert(memory.Query(gap, info) && info.state == 0x2000 && info.protection == 1);
        assert(info.allocation == 0x400000 && !memory.Access(gap, 1, false));
        assert(!memory.Available(gap, 4096));
    }
    assert(memory.Query(0x411000, info) && info.size == 0xf000);
    assert(memory.Query(0x420000, info) && info.state == 0x1000 && info.protection == 4);
    assert(memory.SetProtection(0x420000, 4096, 1));
    assert(memory.Query(0x420000, info) && info.state == 0x1000 && info.protection == 1);
    memory.Forget(0x400000, 0x30000);
    puts("VIRTUAL MEMORY PASS reservations, split states, rollback, protections, zero recommit, ownership, "
         "reuse, VMM descriptor exhaustion and reuse, private 64-KiB reservation granularity, execute-only callbacks, "
         "mapping ownership, overlapping commits, MEM_MAPPED queries, mapping rollback and descriptor reuse");
}
