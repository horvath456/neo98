// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_crt.h"
#include <stdint.h>
// Original MSVCRT.DLL control flow. x87 instructions and thread exception-slot
// lookup remain CPU/thread providers.
namespace Win98CrtFpu {
inline unsigned DecodeStatus(unsigned status)
{
    unsigned result = 0;
    const unsigned flags[] = {0x10, 0x80000, 8, 4, 2, 1};
    for (unsigned i = 0; i < 6; ++i)
        if (status & (1u << i)) result |= flags[i];
    return result;
}
template<class E> unsigned Status(E &e)
{
    e.Wait();
    return DecodeStatus(e.Status());
}
template<class E> unsigned Clear(E &e)
{
    const unsigned status = e.Status();
    e.Clear();
    return DecodeStatus(status);
}
template<class E> uint64_t ToLong(E &e)
{
    e.Wait();
    const unsigned word = e.Control();
    e.Wait();
    e.Control(word | 0x0c00);
    const uint64_t integer = e.IntegerPop();
    e.Control(word);
    return integer;
}
template<class E> void Reset(E &e)
{
    const unsigned pointers = e.ExceptionPointers();
    e.Initialize();
    // Original helper780045CD calls _controlfp(10000h,30000h).
    e.Control(Win98Crt::Control(e.Control(), 0x10000, 0x30000, false).word);
    if (pointers) {
        const unsigned context = e.Read(pointers + 4);
        if (e.Read(context) & 0x10008) {
            e.Write(context + 0x20, 0);
            e.Write(context + 0x24, 0xffff);
        }
    }
}
}
