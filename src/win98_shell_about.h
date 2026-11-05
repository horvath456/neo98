// SPDX-License-Identifier: GPL-2.0-or-later
// ShellAbout queries free memory and USER/GDI resource percentages. Optional
// output pointers may be null. GetFreeSystemResources accepts flags 0–2,
// selecting the minimum, GDI, or USER percentage, and clamps results to 99.
// Neo98 computes usage from its native object tables because it has no
// Windows local heaps.
#pragma once

namespace Win98ShellAbout
{
// The flag words USER.EXE ordinal 284 accepts. Its validation thunk at
//  refuses anything else.
enum Resource
{
    SystemResources = 0,
    GdiResources = 1,
    UserResources = 2,
};
// The original never reports more than 99 per cent free.
constexpr unsigned Ceiling = 0x63;
// The flag word SHELL.DLL hands to KERNEL.169.
constexpr unsigned FreeSpaceFlags = 0x1793;

// The free part in per cent the way USER's helper computes it: 100
// minus the used part, and zero when the record reports no total. The helper
// itself does not clamp;  clamps afterwards, so FreeSystemResources
// below does it.
inline unsigned Percent(unsigned used, unsigned total)
{
    if (!total)
        return 0;
    // The original divides in 32 bits, keeps the low word of the quotient and
    // subtracts it from 100 in a word register, so a used amount above the
    // total wraps. The tables of the port never fill past their size, but the
    // arithmetic stays the original's.
    const unsigned taken = (unsigned)((unsigned long long)used * 100ull / total) & 0xffffu;
    return (100u - taken) & 0xffffu;
}

// USER.EXE ordinal 284 once both percentages are known.
inline unsigned FreeSystemResources(unsigned flag, unsigned gdi, unsigned user)
{
    if (gdi > Ceiling)
        gdi = Ceiling;
    if (user > Ceiling)
        user = Ceiling;
    switch (flag)
    {
    case GdiResources:
        return gdi;
    case UserResources:
        return user;
    case SystemResources:
        return user <= gdi ? user : gdi;
    default:
        // The body falls through with the flag word minus two in AX. The
        // validation thunk never lets such a flag reach it.
        return flag - 2;
    }
}

struct Environment
{
    virtual ~Environment() = default;
    // GDI.609, GdiFreeResources: the free part of the GDI object store.
    virtual unsigned GdiFreePercent() = 0;
    // The smallest free part of USER's object store, USER's heap group.
    virtual unsigned UserFreePercent() = 0;
    // KERNEL.169, GetFreeSpace.
    virtual unsigned FreeSpace() = 0;
    // The two writes the routine makes through the caller's far pointers. A
    // null pointer is skipped, exactly as the two branches of the original do.
    virtual void WriteWord(unsigned at, unsigned value) = 0;
    virtual void WriteDword(unsigned at, unsigned value) = 0;
};

// SHELL.DLL. Returns nothing; the original leaves AX and DX with the
// last value it read, and the 32-bit stub at discards them.
inline void Resources(Environment &e, unsigned resources, unsigned space)
{
    if (resources)
        e.WriteWord(resources, FreeSystemResources(SystemResources, e.GdiFreePercent(),
                                                   e.UserFreePercent()) & 0xffffu);
    if (space)
        e.WriteDword(space, e.FreeSpace());
}
} // namespace Win98ShellAbout
