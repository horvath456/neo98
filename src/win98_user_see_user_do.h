// SPDX-License-Identifier: GPL-2.0-or-later
// UserSeeUserDo gives Win16 callers access to USER local-heap operations and
// selected system data. It takes four WORD arguments: request, first, second,
// and third. Heap allocate, free, and compact requests use the corresponding
// KERNEL services; result width and sign extension depend on the request.
#pragma once

namespace Win98UserSeeUserDo
{
// Request codes and the jump table entry each one takes.
enum Request : unsigned
{
    Allocate = 1,      // 0x1a9e, KERNEL ordinal 5 with (first, third)
    Release = 2,       // 0x1aae, KERNEL ordinal 7 with (first)
    Compact = 3,       // 0x1ab8, KERNEL ordinal 13 with (third)
    WordAt274 = 4,     // 0x1ac2
    WordAt00fa = 5,    // 0x1ac7
    DataSelector = 6,  // 0x1acc, returns DS
    WordAt11a8 = 8,    // 0x1ad0
    PairAt0aa8 = 9,    // 0x1ad5, returns both words as DX:AX
    FarCallThree = 10, // 0x1ade, three trailing zero words
    FarCallOne = 11,   // 0x1b07, one trailing zero word, sign-extended result
    MenuCall = 14,     // 0x1b2d, far call 0x1625:0x5062
    Version = 16,      // 0x1b3f, the constant 0x0001102e
};

struct Environment
{
    virtual ~Environment() = default;
    virtual unsigned LocalAllocate(unsigned flags, unsigned size) = 0;
    virtual unsigned LocalRelease(unsigned handle) = 0;
    virtual unsigned LocalCompact(unsigned size) = 0;
    // USER's own data segment. The offsets are the ones the original reads.
    virtual unsigned DataWord(unsigned offset) = 0;
    virtual unsigned DataPair(unsigned offset) = 0;
    virtual unsigned DataSelector() = 0;
    // The three relocated far calls. "slot" is the data offset of the far
    // pointer the original selects, "zeros" the number of zero words it adds.
    virtual unsigned FarCall(unsigned slot, unsigned value, unsigned zeros) = 0;
    virtual unsigned MenuCall(unsigned value, unsigned word, unsigned mask) = 0;
};

inline unsigned Word(unsigned value)
{
    return value & 0xffffu; // The word paths clear DX.
}
inline unsigned Signed(unsigned value)
{
    // The signed paths sign-extend the result into DX.
    return (value & 0xffffu) | ((value & 0x8000u) ? 0xffff0000u : 0u);
}

// The original selects one of two far pointers by the first argument.
inline unsigned FarSlot(unsigned first)
{
    return (first & 0xffffu) ? 0x27au : 0x276u;
}

inline unsigned Call(Environment &e, unsigned request, unsigned first, unsigned second,
                     unsigned third)
{
    request &= 0xffffu;
    first &= 0xffffu;
    second &= 0xffffu;
    third &= 0xffffu;
    // The original pushes the dword at [bp+6], which is the second and third
    // argument as one value.
    const unsigned pair = (second << 16) | third;
    switch (request)
    {
    case Allocate:
        return Word(e.LocalAllocate(first, third));
    case Release:
        return Word(e.LocalRelease(first));
    case Compact:
        return Word(e.LocalCompact(third));
    case WordAt274:
        return Word(e.DataWord(0x274));
    case WordAt00fa:
        return Word(e.DataWord(0x0fa));
    case DataSelector:
        return Word(e.DataSelector());
    case WordAt11a8:
        return Word(e.DataWord(0x11a8));
    case PairAt0aa8:
        return e.DataPair(0xaa8);
    case FarCallThree:
        return e.FarCall(FarSlot(first), pair, 3);
    case FarCallOne:
        return Signed(e.FarCall(FarSlot(first), pair, 1));
    case MenuCall:
        return Word(e.MenuCall((first << 16) | second, third, 0xff));
    case Version:
        return 0x0001102eu;
    default:
        // 7, 12, 13, 15 and everything outside 1..16 take the default at
        // 0x1a97: AX becomes 0xffff and CWD widens it.
        return 0xffffffffu;
    }
}
} // namespace Win98UserSeeUserDo
