// SPDX-License-Identifier: GPL-2.0-or-later
#include "virtual_memory.h"
#include "win98_session_arena.h"
#include "win98_virtual.h"
#include "win98_vmm_bitmap.h"
#include "win98_vmm_mapping.h"
#include <string.h>
namespace VirtualMemory
{
namespace
{
Space *report_spaces[64]{}; U unreported_spaces=0;
bool Range(U base, U size)
{
    return size && !(base & 4095) && !(size & 4095) &&
           static_cast<unsigned long long>(base) + size <= 0x100000000ull;
}
} // namespace
Space::Space()
{
    for (auto &slot : report_spaces) if (!slot) { slot=this; return; }
    ++unreported_spaces;
}
Space::~Space()
{
    bool removed=false;
    for (auto &slot : report_spaces) if (slot==this) { slot=nullptr; removed=true; break; }
    if (!removed) --unreported_spaces;
    for (U i = 0, limit = RegionLimit(); i < limit; ++i)
        delete[] regions[i].states;
}
U Space::MappedPages() const
{
    U pages=0;
    for (const auto &r:regions) if (r.states)
        for (U i=0;i<r.pages;++i) pages += r.states[i]!=0;
    return pages;
}
bool Space::GlobalMappedPages(U &pages)
{
    if (unreported_spaces) return false;
    unsigned long long total=0;
    for (auto *space:report_spaces) if (space) total+=space->MappedPages();
    if (total>0xffffffffu) return false;
    pages=U(total);return true;
}
U Space::RegionLimit() const { return Win98VmmBitmap::Last(descriptors, RegionCapacity) + 1u; }
void Space::Remove(Region &r)
{
    if (r.block)
        arena.Release(r.block);
    delete[] r.states;
    Win98VmmBitmap::Release(descriptors, static_cast<U>(&r - regions) + 1u);
    r = {};
}
U Space::Protection(U permissions)
{
    return permissions & 4   ? (permissions & 2   ? 0x40
                                : permissions & 1 ? 0x20
                                                  : 0x10)
           : permissions & 2 ? 4
           : permissions & 1 ? 2
                             : 1;
}
U Space::Permissions(U protection)
{
    switch (protection)
    {
    case 2:
        return 1;
    case 4:
        return 3;
    case 0x10:
        return 4;
    case 0x20:
        return 5;
    case 0x40:
        return 7;
    default:
        return 0;
    }
}
bool Space::Overlaps(U base, U size) const
{
    const auto end = static_cast<unsigned long long>(base) + size;
    for (U i = 0, limit = RegionLimit(); i < limit; ++i)
    {
        const auto &r = regions[i];
        if (r.states && base < static_cast<unsigned long long>(r.base) + r.pages * 4096 && r.base < end)
            return true;
    }
    return false;
}
const Space::Region *Space::Containing(U base, U size) const
{
    // An API call reads its arguments on the stack, the thread block and a
    // heap or two: the regions of the last lookups answer most of them
    // without the scan.
    const auto holds = [base, size](const Region &r) {
        return r.states && base >= r.base && base - r.base < r.pages * 4096 &&
               size <= r.pages * 4096 - (base - r.base);
    };
    for (U h = 0; h < 4; ++h)
        if (holds(regions[recent_regions[h] % RegionCapacity]))
            return &regions[recent_regions[h] % RegionCapacity];
    for (U i = 0, limit = RegionLimit(); i < limit; ++i)
        if (holds(regions[i]))
        {
            recent_regions[3] = recent_regions[2];
            recent_regions[2] = recent_regions[1];
            recent_regions[1] = recent_regions[0];
            recent_regions[0] = i;
            return &regions[i];
        }
    return nullptr;
}
Space::Region *Space::Containing(U base, U size)
{
    return const_cast<Region *>(static_cast<const Space *>(this)->Containing(base, size));
}
bool Space::Exclude(U base, U size)
{
    if (!Range(base, size))
        return false;
    for (U i = 0; i < exclusion_count; ++i)
        if (exclusions[i].base == base && exclusions[i].size == size)
            return true;
    if (exclusion_count == 16 || !arena.Reserve(base >> 12, size >> 12))
        return false;
    exclusions[exclusion_count++] = {base, size};
    return true;
}
bool Space::Available(U base, U size) const
{
    const auto end = static_cast<unsigned long long>(base) + size;
    if (!size || end > 0x100000000ull)
        return false;
    const U page = base >> 12, limit = static_cast<U>((end + 4095) >> 12);
    return arena.Available(page, limit - page);
}
Space::Region *Space::Register(U base, U size, U protection, Owner owner, U flags)
{
    if (!Range(base, size) || Overlaps(base, size))
        return nullptr;
    bool excluded = false;
    const auto end = static_cast<unsigned long long>(base) + size;
    for (U i = 0; i < exclusion_count; ++i)
    {
        const auto &range = exclusions[i];
        const auto range_end = static_cast<unsigned long long>(range.base) + range.size;
        if (base < range_end && range.base < end)
        {
            // Native stacks/TEBs may map inside a permanent exclusion. A mapping
            // crossing its boundary needs a more general overlap policy.
            if (owner != Owner::Tracked || base < range.base || end > range_end)
                return nullptr;
            excluded = true;
            break;
        }
    }
    const U identifier = Win98VmmBitmap::Claim(descriptors, RegionCapacity);
    if (!identifier)
        return nullptr;
    auto *block = excluded ? nullptr : arena.Reserve(base >> 12, size >> 12, flags);
    if (!excluded && !block)
    {
        Win98VmmBitmap::Release(descriptors, identifier);
        return nullptr;
    }
    auto *states = new unsigned char[size >> 12];
    if (!states)
    {
        if (block)
            arena.Release(block);
        Win98VmmBitmap::Release(descriptors, identifier);
        return nullptr;
    }
    memset(states, owner == Owner::Tracked ? protection : 0, size >> 12);
    auto &region = regions[identifier - 1u];
    region = {base, size >> 12, protection, states, owner, block};
    return &region;
}
bool Space::Track(U base, U size, U protection) { return Register(base, size, protection, Owner::Tracked, 0); }
U Space::ReserveSession(Backend &e, U page, U pages, U flags, bool mapped)
{
    return Reserve(e, page, pages, flags, mapped ? Owner::Mapping : Owner::Heap);
}
void Space::Forget(U base, U size)
{
    auto *r = Containing(base, size);
    if (r && r->base == base && r->pages == size >> 12 && r->owner == Owner::Tracked)
        Remove(*r);
}
bool Space::SetProtection(U base, U size, U protection)
{
    auto *r = Containing(base, size);
    if (!Range(base, size) || !r)
        return false;
    memset(r->states + ((base - r->base) >> 12), protection, size >> 12);
    return true;
}
bool Space::Executable(U address) const
{
    const auto *r = Containing(address, 1);
    return r && (Permissions(r->states[(address - r->base) >> 12]) & 4);
}
bool Space::Access(U base, U size, bool write) const
{
    if (!size)
        return true;
    if (base > ~0u - (size - 1))
        return false;
    while (size)
    {
        const auto *r = Containing(base, 1);
        if (!r)
            return false;
        const U index = (base - r->base) >> 12;
        if (!(Permissions(r->states[index]) & (write ? 2 : 1)))
            return false;
        const U n = size < 4096 - (base & 4095) ? size : 4096 - (base & 4095);
        base += n;
        size -= n;
    }
    return true;
}
U Space::Reserve(Backend &e, U page, U pages, U flags)
{
    return Reserve(e, page, pages, flags, Owner::VirtualAllocation);
}
U Space::FindPrivate(U size) const
{
    // Lowest free interval of the private arena, with the same start and
    // length rounding an automatic private reservation uses. The loader
    // places fixed ranges first and then asks for this address, so a range
    // that an image already holds is not offered again.
    if (!size || size > 0x7fc00000u)
        return 0;
    const U pages = ((((size + 4095) >> 12) + 15u) & ~15u);
    const U found = arena.Find(pages, 16, 0x400, 0x80000);
    return found == ~0u ? 0 : found << 12;
}
U Space::ReserveMapping(Backend &e, U page, U pages, U flags)
{
    return Reserve(e, page, pages, flags, Owner::Mapping);
}
U Space::ReserveHeap(Backend &e, U page, U pages)
{
    if (page != 0x80000400 && page != 0x80060000)
        return ~0u;
    return Reserve(e, page, pages, 0x60000, Owner::Heap);
}
U Space::Reserve(Backend &e, U page, U pages, U flags, Owner owner)
{
    // Write-watch requires dirty-page tracking, not an ignored success flag.
    if (!pages || pages > 0x7fc00 || flags & ~0x60000u)
        return ~0u;
    U size = pages * 4096, base = 0;
    if (page == 0x80000400 || page == 0x80060000)
    {
        // Original _PageReserve, THCR object. Private reservations
        // round the entire length, as well as the start, to sixteen pages.
        if (page == 0x80000400)
            size = ((pages + 15u) & ~15u) * 4096;
        const bool private_arena = page == 0x80000400;
        // The lower quarter of the shared arena, 80000000h to 8FFFFFFFh,
        // belongs to the session: the heaps with HEAP_SHARED and the views of
        // named file mappings get their address there, and that address has to
        // be free in every process. An automatic reservation of this process
        // therefore starts above it. A caller that names an address inside the
        // session window still gets it, and collides with the session if the
        // range is taken.
        const U shared_floor = Win98SessionArena::Limit >> 12;
        // A0000000h to AFFFFFFFh of the shared arena holds the bits of the
        // DIB sections, which the session places as well
        // (Application::AllocateSessionDib); an automatic reservation of this
        // process passes that window over, too.
        U found = private_arena ? arena.Find(size >> 12, 16, 0x400, 0x80000)
                                : arena.Find(size >> 12, 1, shared_floor, 0xa0000);
        if (found == ~0u && !private_arena)
            found = arena.Find(size >> 12, 1, 0xb0000, 0xbfc00);
        if (found == ~0u)
            return ~0u;
        base = found << 12;
    }
    else
    {
        if (page >= 0xbfc00)
            return ~0u;
        const bool private_arena = page <= 0x7ffff;
        base = page << 12;
        if (private_arena)
        {
            base &= ~65535u;
            size = (((page & 15u) + pages + 15u) & ~15u) * 4096;
        }
        const U limit = private_arena ? 0x80000000 : 0xbfc00000;
        if (base < 0x400000 || size > limit - base)
            return ~0u;
    }
    // Exclusions and mappings have already been removed from the free list.
    // A backend may still reject an address; no interval is consumed on rejection.
    if (!e.Available(base, size))
        return ~0u;
    return Register(base, size, Win98Virtual::PageProtection(flags), owner, (flags | (flags >> 17)) & 0xfff)
               ? base
               : ~0u;
}
U Space::Commit(Backend &e, U page, U count, U flags)
{
    return Commit(e, page, count, flags, Owner::VirtualAllocation);
}
U Space::CommitMapping(Backend &e, U page, U count, U flags)
{
    return Commit(e, page, count, flags, Owner::Mapping);
}
U Space::CommitHeap(Backend &e, U page, U count)
{
    return Commit(e, page, count, 0x60000, Owner::Heap);
}
U Space::Commit(Backend &e, U page, U count, U flags, Owner owner)
{
    if (!count || page >= 0xc0000 || count > 0xc0000 - page || flags & ~0x60000u)
        return 0;
    auto *r = Containing(page << 12, count << 12);
    if (!r || r->owner != owner)
        return 0;
    // Win98's VMM protection bits have no execute-disable bit. Keep actual
    // backing permissions here; Query converts them to the legacy 2/4 values.
    const U permissions = flags & 0x40000 ? (flags & 0x20000 ? 7 : 5) : 0;
    const U first = page - (r->base >> 12), protection = Protection(permissions);
    U added = 0;
    for (U i = first; i < first + count; ++i)
        added += !r->states[i];
    if (added > 65536 - committed)
        return 0; // 256 MiB committed private/shared backing in this prototype.
    struct Snapshot
    {
        unsigned char *states = nullptr;
        ~Snapshot() { delete[] states; }
    } previous;
    if (owner == Owner::Mapping)
    {
        previous.states = new unsigned char[count];
        if (!previous.states)
            return 0;
        memcpy(previous.states, r->states + first, count);
    }
    auto rollback = [&]()
    {
        for (U j = first; j < first + count;)
        {
            if (r->states[j] & 0x80)
            {
                const U start = j;
                while (j < first + count && r->states[j] & 0x80)
                    ++j;
                e.Unmap(r->base + start * 4096, (j - start) * 4096);
                memset(r->states + start, 0, j - start);
            }
            else if (previous.states && r->states[j] != previous.states[j - first])
            {
                const U start = j, old = previous.states[j - first];
                while (j < first + count && !(r->states[j] & 0x80) &&
                       r->states[j] != previous.states[j - first] && previous.states[j - first] == old)
                    ++j;
                e.Protect(r->base + start * 4096, (j - start) * 4096, Permissions(old));
                memset(r->states + start, old, j - start);
            }
            else
                ++j;
        }
    };
    for (U i = first; i < first + count;)
    {
        if (r->states[i])
        {
            ++i;
            continue;
        }
        const U begin = i;
        while (i < first + count && !r->states[i])
            ++i;
        if (!e.Map(r->base + begin * 4096, (i - begin) * 4096, permissions))
        {
            rollback();
            return 0;
        }
        memset(r->states + begin, protection | 0x80, i - begin);
    }
    if (owner == Owner::Mapping)
        for (U i = first; i < first + count; ++i)
        {
            if (!previous.states[i - first])
                continue;
            const U before = Permissions(r->states[i]);
            // Specialize the original VMM existing-PTE WRITE decision to native
            // page permissions. NEO98 stores commitment separately from access.
            const auto entry = Win98VmmMapping::Existing(0x200 | (before & 2), flags, 0);
            // Native projection after an explicit VirtualProtect may also restore
            // requested READ/EXEC permissions; that part is not the original PTE
            // branch. The rejection mask is zero for these validated operations.
            const U merged = (before | (permissions & 5)) | (entry.entry & 2);
            if (merged == before)
                continue;
            if (!e.Protect(r->base + i * 4096, 4096, merged))
            {
                rollback();
                return 0;
            }
            r->states[i] = Protection(merged);
        }
    for (U i = first; i < first + count; ++i)
        r->states[i] &= ~0x80u;
    committed += added;
    return 1;
}
bool Space::Decommit(Backend &e, Region &r, U first, U count)
{
    for (U i = first; i < first + count;)
    {
        if (!r.states[i])
        {
            ++i;
            continue;
        }
        const U begin = i;
        while (i < first + count && r.states[i])
            ++i;
        if (!e.Unmap(r.base + begin * 4096, (i - begin) * 4096))
            return false;
        memset(r.states + begin, 0, i - begin);
        committed -= i - begin;
    }
    return true;
}
U Space::Decommit(Backend &e, U page, U count)
{
    if (!count || page >= 0xc0000 || count > 0xc0000 - page)
        return 0;
    auto *r = Containing(page << 12, count << 12);
    return r && r->owner == Owner::VirtualAllocation && Decommit(e, *r, page - (r->base >> 12), count);
}
U Space::Release(Backend &e, U base)
{
    return Release(e, base, Owner::VirtualAllocation);
}
U Space::ReleaseMapping(Backend &e, U base)
{
    return Release(e, base, Owner::Mapping);
}
U Space::DecommitHeap(Backend &e, U page, U count)
{
    if (!count || page >= 0xc0000 || count > 0xc0000 - page)
        return 0;
    auto *r = Containing(page << 12, count << 12);
    return r && r->owner == Owner::Heap && Decommit(e, *r, page - (r->base >> 12), count);
}
U Space::ReleaseHeap(Backend &e, U base)
{
    return Release(e, base, Owner::Heap);
}
U Space::Release(Backend &e, U base, Owner owner)
{
    auto *r = Containing(base, 1);
    if (!r || r->owner != owner || r->base != base || !Decommit(e, *r, 0, r->pages))
        return 0;
    Remove(*r);
    return 1;
}
U Space::Protect(Backend &e, U address, U size, U flags)
{
    if (!size || address > ~0u - size || size > 0x7fc00000)
        return ~0u;
    const U base = address & ~4095u, bytes = ((address & 4095) + size + 4095) & ~4095u;
    auto *r = Containing(base, bytes);
    if (!r)
        return ~0u;
    const U first = (base - r->base) >> 12, count = bytes >> 12;
    for (U i = first; i < first + count; ++i)
        if (!r->states[i])
            return ~0u;
    const U previous = Permissions(r->states[first]);
    const U permissions = flags & 0x40000 ? (flags & 0x20000 ? 7 : 5) : 0;
    if (!e.Protect(base, bytes, permissions))
        return ~0u;
    memset(r->states + first, Protection(permissions), count);
    return (previous & 5 ? 0x40000 : 0) | (previous & 2 ? 0x20000 : 0);
}
bool Space::Query(U address, Info &info) const
{
    const U base = address & ~4095u;
    if (base >= 0xc0000000)
        return false;
    const auto *r = Containing(base, 1);
    if (!r)
    {
        U end = 0xc0000000;
        for (U i = 0, limit = RegionLimit(); i < limit; ++i)
        {
            const auto &candidate = regions[i];
            if (candidate.states && candidate.base > base && candidate.base < end)
                end = candidate.base;
        }
        bool excluded = false;
        U allocation = 0;
        for (U i = 0; i < exclusion_count; ++i)
        {
            const auto &range = exclusions[i];
            const auto range_end = static_cast<unsigned long long>(range.base) + range.size;
            if (base >= range.base && base < range_end)
            {
                excluded = true;
                allocation = range.base;
                if (range_end < end)
                    end = static_cast<U>(range_end);
            }
            else if (base < range.base && range.base < end)
                end = range.base;
        }
        info = {base,
                allocation,
                excluded ? 1u : 0u,
                end - base,
                excluded ? 0x2000u : 0x10000u,
                0,
                excluded ? 0x20000u : 0u};
        return true;
    }
    auto canonical = [](U p)
    {
        if (!p)
            return 0u;
        const U permissions = Permissions(p);
        return permissions & 2 ? 4u : permissions & 5 ? 2u : 1u;
    };
    U i = (base - r->base) >> 12, end = i + 1;
    const U p = canonical(r->states[i]);
    while (end < r->pages && canonical(r->states[end]) == p)
        ++end;
    // Win98 reports PAGE_NOACCESS for reserved holes of a loaded image.
    // VirtualAlloc reservations keep their existing zero protection value.
    const U protection = p ? p : r->owner == Owner::Tracked ? 1u : 0u;
    info = {base, r->base, canonical(r->protection), (end - i) * 4096, p ? 0x1000u : 0x2000u, protection,
            r->owner == Owner::Mapping ? 0x40000u : 0x20000u};
    return true;
}
} // namespace VirtualMemory
