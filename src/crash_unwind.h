// SPDX-License-Identifier: GPL-2.0-or-later
// Whether a call instruction ends directly in front of an address. A crash
// report derives its call chain from the stack; without this check every word
// that points into a code module counted as a frame, so data and function
// pointers lying on the stack showed up as return addresses. An
// unwinder without unwind data decides it exactly this way, by looking at the
// bytes before the candidate. Header without a translation unit so the report
// and its test share the same decision.
#pragma once

namespace CrashUnwind
{
// `window` holds the seven bytes in front of the address; the last `available`
// of them are valid and the last one sits at index 6. Answers the length of the
// call in bytes, or zero. Covered are the forms compilers of that time emit:
// `E8 rel32`, the far `9A ptr16:32` and the whole `FF /2` and `FF /3` group with
// every addressing mode. Seven readable bytes cover all of them, so a zero is
// final there. Prefixes need no handling: behind a segment override the call
// also decodes at a shorter length, and the question is only whether one ends
// here at all.
inline unsigned CallLength(const unsigned char window[7], unsigned available)
{
    if (available > 7) available = 7;
    for (unsigned length = 2; length <= available; ++length)
    {
        const unsigned char *p = window + 7 - length;
        if (p[0] == 0xe8) { if (length == 5) return length; continue; }
        if (p[0] == 0x9a) { if (length == 7) return length; continue; }
        if (p[0] != 0xff) continue;
        const unsigned char modrm = p[1];
        const unsigned reg = (modrm >> 3) & 7, mod = modrm >> 6, rm = modrm & 7;
        if (reg != 2 && reg != 3) continue;      // /2 near call, /3 far call
        if (reg == 3 && mod == 3) continue;      // no far call through a register
        unsigned size = 2;                       // FF and ModRM
        if (mod != 3 && rm == 4) ++size;          // SIB byte
        if (mod == 1) size += 1;
        else if (mod == 2) size += 4;
        else if (mod == 0 && rm == 5) size += 4;  // disp32 without a base
        if (size == length) return length;
    }
    return 0;
}
} // namespace CrashUnwind
