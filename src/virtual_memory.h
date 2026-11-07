// SPDX-License-Identifier: GPL-2.0-or-later
// Native NEO98 address-space metadata. Independent of the CPU backend.
#pragma once
#include "win98_vmm_arena.h"
namespace VirtualMemory
{
using U = unsigned;
struct Info
{
    U base, allocation, allocation_protection, size, state, protection, type;
};
class Backend
{
  public:
    virtual bool Available(U base, U size) = 0;
    virtual bool Map(U base, U size, U permissions) = 0;
    virtual bool Unmap(U base, U size) = 0;
    virtual bool Protect(U base, U size, U permissions) = 0;

  protected:
    ~Backend() = default;
};
class Space
{
  public:
    Space();
    ~Space();
    Space(const Space &) = delete;
    Space &operator=(const Space &) = delete;
    bool Exclude(U base, U size);
    bool Available(U base, U size) const;
    bool Track(U base, U size, U protection);
    void Forget(U base, U size);
    // A block of the session arena at an address the session already chose.
    // `mapped` picks the owner class VirtualQuery reports: MEM_MAPPED for the
    // view of a named file mapping, MEM_PRIVATE for a heap from HeapCreate
    // with HEAP_SHARED. Commit, decommit and release then run through the
    // ordinary mapping and heap entries above, with a backend that hands
    // Unicorn the host bytes of the block.
    U ReserveSession(Backend &, U page, U pages, U flags, bool mapped);
    bool SetProtection(U base, U size, U protection);
    bool Overlaps(U base, U size) const;
    // Lowest free private-arena address for a range of this size, 0 when none.
    U FindPrivate(U size) const;
    bool Access(U base, U size, bool write) const;
    bool Executable(U address) const;
    U Reserve(Backend &, U page, U pages, U flags);
    U Commit(Backend &, U page, U pages, U flags);
    U Decommit(Backend &, U page, U pages);
    U Release(Backend &, U base);
    // Same arena and normalized VMM protection bits as Reserve/Commit. Only
    // these entry points may commit or release a mapping-owned reservation.
    U ReserveMapping(Backend &, U page, U pages, U protection_flags);
    U CommitMapping(Backend &, U page, U pages, U protection_flags);
    U ReleaseMapping(Backend &, U base);
    U ReserveHeap(Backend &, U arena, U pages);
    U CommitHeap(Backend &, U page, U pages);
    U DecommitHeap(Backend &, U page, U pages);
    U ReleaseHeap(Backend &, U base);
    U Protect(Backend &, U address, U size, U flags);
    bool Query(U address, Info &) const;
    U CommittedPages() const { return committed; }
    U MappedPages() const;
    U FreePrivatePages() const { return arena.FreePages(0x400, 0x80000); }
    static bool GlobalMappedPages(U &pages);
    static U Protection(U permissions);
    static U Permissions(U protection);

  private:
    enum class Owner : unsigned char { Tracked, VirtualAllocation, Mapping, Heap };
    // The prototype kept 256 regions and 576 interval descriptors, a limit of
    // this port and not of Win98's VMM, whose arena descriptors come from its
    // heap. A game with its DLLs, heaps and dozens of DIB sections ran out of
    // regions, and CreateDIBSection failed (sb2.exe).
    // mallocng maps small groups independently; 192 MiB of 1000-byte
    // allocations needs more than 6000 live regions.
    static constexpr U RegionCapacity = 16384;
    struct Region
    {
        U base = 0, pages = 0, protection = 0;
        unsigned char *states = nullptr;
        Owner owner = Owner::Tracked;
        Win98VmmArena::Block *block = nullptr;
    } regions[RegionCapacity];
    struct Exclusion
    {
        U base = 0, size = 0;
    } exclusions[16];
    U exclusion_count = 0;
    Win98VmmArena::Arena<RegionCapacity * 2 + 64> arena;
    U descriptors[RegionCapacity / 32]{};
    U committed = 0;
    // The regions the last lookups found, newest first. Only hints:
    // Containing checks them like any other entry, so a stale value costs a
    // scan and nothing else.
    mutable U recent_regions[4] = {};
    U RegionLimit() const;
    void Remove(Region &);
    Region *Register(U base, U size, U protection, Owner, U flags);
    Region *Containing(U base, U size);
    const Region *Containing(U base, U size) const;
    U Reserve(Backend &, U page, U pages, U flags, Owner);
    U Commit(Backend &, U page, U pages, U flags, Owner);
    U Release(Backend &, U base, Owner);
    bool Decommit(Backend &, Region &, U first, U count);
};
} // namespace VirtualMemory
