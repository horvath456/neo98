// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
// Native instruction provider. Operates on
// the explicit80-bit x87 encoding; no host floating arithmetic or casts.
namespace X86FpuInteger {
struct Result { uint64_t bits; unsigned flags; };
inline Result Truncate(uint64_t significand, unsigned sign_exponent, bool empty)
{
    constexpr uint64_t indefinite = UINT64_C(0x8000000000000000);
    const unsigned exponent = sign_exponent & 0x7fff;
    const bool negative = sign_exponent & 0x8000;
    if (empty) return {indefinite, 0x41}; // IE and stack fault.
    if (exponent == 0x7fff || (exponent && !(significand >> 63)))
        return {indefinite, 1}; // NaN, infinity or unsupported unnormal.
    if (!significand) return {0, 0};
    const int power = exponent ? int(exponent) - 16383 : -16382;
    if (power < 0) return {0, 0x20};
    if (power > 63 || (power == 63 && (!negative || significand != indefinite)))
        return {indefinite, 1};
    if (power == 63) return {indefinite, 0}; // Exactly -2^63 is representable.
    const unsigned shift = 63 - unsigned(power);
    const uint64_t magnitude = significand >> shift;
    const unsigned flags = (significand << (64 - shift)) ? 0x20 : 0;
    return {negative ? uint64_t(0) - magnitude : magnitude, flags};
}
inline unsigned ClearStatus(unsigned status) { return status & 0x7f00; }
inline unsigned PopStatus(unsigned status, unsigned flags)
{
    const unsigned top = ((status >> 11) + 1) & 7;
    return ((status & ~0x3a00u) | (top << 11) | flags) & 0xffff;
}
inline bool Pending(unsigned control, unsigned status)
{
    return (status & 0x80) || ((status & ~control) & 0x3f);
}
}
