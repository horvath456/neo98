// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_crt.h"
namespace Win98Crt
{
unsigned DecodeControl(unsigned word)
{
    const unsigned exceptions[] = {0x10, 0x80000, 8, 4, 2, 1};
    unsigned value = ((word >> 10) & 3) << 8;
    for (unsigned i = 0; i < 6; ++i)
        if (word & (1u << i))
            value |= exceptions[i];
    const unsigned precision = (word >> 8) & 3;
    if (!precision)
        value |= 0x20000;
    else if (precision == 2)
        value |= 0x10000;
    if (word & 0x1000)
        value |= 0x40000;
    return value;
}
unsigned EncodeControl(unsigned value)
{
    const unsigned exceptions[] = {0x10, 0x80000, 8, 4, 2, 1};
    unsigned word = ((value >> 8) & 3) << 10;
    for (unsigned i = 0; i < 6; ++i)
        if (value & exceptions[i])
            word |= 1u << i;
    const unsigned precision = value & 0x30000;
    if (!precision)
        word |= 0x300;
    else if (precision == 0x10000)
        word |= 0x200;
    if (value & 0x40000)
        word |= 0x1000;
    return word;
}
ControlResult Control(unsigned word, unsigned requested, unsigned mask, bool control87)
{
    if (!control87)
        mask &= ~0x80000u;
    const unsigned value = (DecodeControl(word) & ~mask) | (requested & mask);
    return {value, EncodeControl(value)};
}
Prolog ExceptionProlog(unsigned sp, unsigned return_address, unsigned eax, unsigned ebp, unsigned previous)
{
    return {{previous, eax, 0xffffffffu, ebp}, sp - 12, sp, return_address, return_address, sp - 12};
}
} // namespace Win98Crt
