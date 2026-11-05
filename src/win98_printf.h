// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
namespace Win98Printf
{
// The Unicode to ANSI walk the formatter calls. It answers the
// number of bytes it wrote, one for a single-byte code page and two for a
// lead/trail pair, and zero when the character is in none of the ranges of
// the code page table. The formatter writes an underscore for that case.
struct Ansi
{
    virtual unsigned Narrow(unsigned character, unsigned char *bytes) = 0;

  protected:
    ~Ansi() = default;
};
// ANSI and wide paths of Win98 KERNEL32. Without a code page the wide
// conversions answer supported = false.
unsigned Format(Win98Arrays::Memory &, unsigned output, unsigned format, unsigned arguments, bool &supported,
                Ansi *ansi = nullptr);
} // namespace Win98Printf
