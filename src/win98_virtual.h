// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Virtual
{
// KERNEL32: PAGE_NOCACHE is ignored; execution and reading share a VMM flag.
template <class E> unsigned Protection(E &e, unsigned value)
{
    switch (value & ~0x200u)
    {
    case 1:
        return 0;
    case 2:
    case 0x10:
    case 0x20:
        return 0x40000;
    case 4:
    case 0x40:
        return 0x60000;
    default:
        e.Error(87);
        return ~0u;
    }
}
inline unsigned PageProtection(unsigned flags) { return !(flags & 0x40000) ? 1 : (flags & 0x20000) ? 4 : 2; }
// Complete public VirtualAlloc body; VMM services are explicit providers.
template <class E>
unsigned Allocate(E &e, unsigned address, unsigned size, unsigned type, unsigned protection)
{
    if (size > 0x7fc00000)
    {
        e.Error(8);
        return 0;
    }
    const unsigned pages = ((address & 4095) + size + 4095) >> 12;
    const unsigned arena = type & 0x8000000 ? 0x80060000 : 0x80000400;
    type &= ~0x8100000u;
    if (type != 0x1000 && type != 0x2000 && type != 0x3000 && type != 0x202000 && type != 0x203000)
    {
        e.Error(87);
        return 0;
    }
    unsigned flags = Protection(e, protection);
    if (flags == ~0u)
        return 0;
    flags |= type & 0x200000;
    if (!address)
    {
        address = e.Reserve(arena, pages, flags);
        if (address == ~0u)
        {
            e.Error(8);
            return 0;
        }
        if (!(type & 0x1000) || e.Commit(address >> 12, pages, 1, 0, flags))
            return address;
        e.Error(8);
        e.Release(address, 0);
        return 0;
    }
    if (address > 0xbfffffff)
    {
        e.Error(487);
        return 0;
    }
    const bool reserved = type & 0x2000;
    if (reserved)
    {
        address = e.Reserve(address >> 12, pages, flags);
        if (address == ~0u)
        {
            e.Error(8);
            return 0;
        }
    }
    if (!(type & 0x1000))
        return address;
    address &= ~4095u;
    if (e.Commit(address >> 12, pages, 1, 0, flags))
        return address;
    e.Error(8);
    if (reserved)
        e.Release(address & ~65535u, 0);
    return 0;
}
// Win98 rejects MEM_DECOMMIT with a zero size.
template <class E> unsigned Free(E &e, unsigned address, unsigned size, unsigned type)
{
    if (size > 0x7fc00000 || address > 0xbfffffff)
    {
        e.Error(487);
        return 0;
    }
    if (type == 0x8000 && !size)
        return e.Release(address, 0);
    if (type == 0x4000 && size)
        return e.Decommit(address >> 12, ((address & 4095) + size + 4095) >> 12, 0);
    e.Error(87);
    return 0;
}
} // namespace Win98Virtual
