// SPDX-License-Identifier: GPL-2.0-or-later
// WIN87EM arithmetic helpers. __FPMATH selects a subfunction through BX.
// Function 6 rounds to an integer using AX's rounding-mode bits and returns
// the temporary control word. Function 7 converts to a signed doubleword in
// DX:AX. Function 8 accumulates the six exception bits and returns status
// masked with 0x1FFF.
// Use integer arithmetic because ARM64 host floating point cannot represent
// x87 80-bit values exactly.
#pragma once
#include <stdint.h>
namespace Win98Win87Em {
// x87 status exception bits collected by subfunction 8.
enum : unsigned
{
    Invalid = 0x01,
    Denormal = 0x02,
    ZeroDivide = 0x04,
    Overflow = 0x08,
    Underflow = 0x10,
    Precision = 0x20
};
// Unicorn x87 value layout: 64-bit significand with explicit integer bit,
// followed by a word containing the 15-bit exponent biased by 16383 and the
// sign bit.
struct Extended
{
    uint64_t mantissa = 0;
    unsigned word = 0;
    unsigned Exponent() const { return word & 0x7fff; }
    bool Negative() const { return (word & 0x8000) != 0; }
    void Set(uint64_t significand, unsigned exponent, bool negative)
    {
        mantissa = significand;
        word = (exponent & 0x7fff) | (negative ? 0x8000u : 0u);
    }
    bool Zero() const { return !Exponent() && !mantissa; }
    bool Special() const { return Exponent() == 0x7fff; }
    bool Denormalized() const { return !Exponent() && mantissa; }
};
// Control-word RC bits 10:11 select nearest-even, downward, upward or
// truncation. any indicates a nonzero fractional part; odd is the integer
// part's low bit.
inline bool RoundsAway(unsigned rounding, bool any, bool above_half, bool exactly_half, bool odd,
                       bool negative)
{
    switch (rounding & 3)
    {
    case 0: return above_half || (exactly_half && odd);
    case 1: return any && negative;
    case 2: return any && !negative;
    default: return false;
    }
}
// FRNDINT: return the exception bits set by the operation.
inline unsigned RoundToInteger(Extended &value, unsigned control)
{
    const unsigned rounding = (control >> 10) & 3;
    // Preserve zero, infinity and NaN. Signaling-NaN exception delivery is
    // not implemented here.
    if (value.Special() || value.Zero())
        return 0;
    unsigned flags = value.Denormalized() ? Denormal : 0u;
    const int unbiased = value.Denormalized() ? -16382 : (int)value.Exponent() - 16383;
    if (unbiased >= 63)
        return flags; // The value already has no fractional part.
    const bool negative = value.Negative();
    if (unbiased < 0)
    {
        // Magnitude below one rounds to zero or one.
        const bool exactly_half = unbiased == -1 && value.mantissa == UINT64_C(0x8000000000000000);
        const bool above_half = unbiased == -1 && value.mantissa > UINT64_C(0x8000000000000000);
        if (RoundsAway(rounding, true, above_half, exactly_half, false, negative))
            value.Set(UINT64_C(0x8000000000000000), 16383, negative);
        else
            value.Set(0, 0, negative);
        return flags | Precision;
    }
    const unsigned shift = 63 - (unsigned)unbiased; // One to 63 fractional bits
    const uint64_t step = UINT64_C(1) << shift;
    const uint64_t fraction = value.mantissa & (step - 1);
    if (!fraction)
        return flags;
    uint64_t integer = value.mantissa - fraction;
    unsigned exponent = value.Exponent();
    if (RoundsAway(rounding, true, fraction > (step >> 1), fraction == (step >> 1),
                   ((value.mantissa >> shift) & 1) != 0, negative))
    {
        const uint64_t raised = integer + step;
        if (raised < integer)
        {
            integer = UINT64_C(0x8000000000000000);
            ++exponent;
        }
        else
            integer = raised;
    }
    value.Set(integer, exponent, negative);
    return flags | Precision;
}
// FISTP doubleword conversion. An out-of-range rounded result raises
// invalid-operation status and stores the integer-indefinite value
// 0x80000000.
inline unsigned ToInteger32(const Extended &value, unsigned control, unsigned &out)
{
    Extended rounded = value;
    const unsigned flags = RoundToInteger(rounded, control);
    out = 0x80000000u;
    if (rounded.Special())
        return flags | Invalid;
    if (rounded.Zero())
    {
        out = 0;
        return flags;
    }
    const int unbiased = (int)rounded.Exponent() - 16383;
    if (unbiased < 0 || unbiased > 31)
        return flags | Invalid;
    const uint64_t magnitude = rounded.mantissa >> (63 - unbiased);
    if (magnitude > (rounded.Negative() ? UINT64_C(0x80000000) : UINT64_C(0x7fffffff)))
        return flags | Invalid;
    out = rounded.Negative() ? (unsigned)(0u - (unsigned)magnitude) : (unsigned)magnitude;
    return flags;
}

// x87 stack access. FP0..FP7 are physical registers; FPSW bits 11:13 select
// ST(0). Each register has two FPTAG bits: valid, zero, special or empty.
inline bool Slot(X86::Cpu *cpu, unsigned &top, unsigned &tags, unsigned &status)
{
    unsigned raw_status = 0, raw_tags = 0;
    if (cpu->Get(X86::Reg::Fpsw, raw_status) != X86::CpuError::Ok)
        return false;
    if (cpu->Get(X86::Reg::Fptag, raw_tags) != X86::CpuError::Ok)
        return false;
    status = raw_status;
    tags = raw_tags;
    top = (raw_status >> 11) & 7;
    return true;
}
inline bool Load(X86::Cpu *cpu, Extended &value, bool &empty)
{
    unsigned top = 0, tags = 0, status = 0;
    if (!Slot(cpu, top, tags, status))
        return false;
    empty = ((tags >> (top * 2)) & 3) == 3;
    uint64_t raw[2] = {};
    if (cpu->GetWide(X86::Reg::Fp0 + top, raw) != X86::CpuError::Ok)
        return false;
    value.mantissa = raw[0];
    value.word = (unsigned)(raw[1] & 0xffff);
    return true;
}
inline bool Store(X86::Cpu *cpu, const Extended &value)
{
    unsigned top = 0, tags = 0, status = 0;
    if (!Slot(cpu, top, tags, status))
        return false;
    uint64_t raw[2] = {value.mantissa, value.word};
    if (cpu->SetWide(X86::Reg::Fp0 + top, raw) != X86::CpuError::Ok)
        return false;
    const unsigned tag = value.Special() ? 2u : value.Zero() ? 1u : 0u;
    const uint16_t written = (uint16_t)((tags & ~(3u << (top * 2))) | (tag << (top * 2)));
    return cpu->Set(X86::Reg::Fptag, written) == X86::CpuError::Ok;
}
inline bool Pop(X86::Cpu *cpu)
{
    unsigned top = 0, tags = 0, status = 0;
    if (!Slot(cpu, top, tags, status))
        return false;
    const uint16_t emptied = (uint16_t)(tags | (3u << (top * 2)));
    if (cpu->Set(X86::Reg::Fptag, emptied) != X86::CpuError::Ok)
        return false;
    const uint16_t moved = (uint16_t)((status & ~0x3800u) | (((top + 1) & 7) << 11));
    return cpu->Set(X86::Reg::Fpsw, moved) == X86::CpuError::Ok;
}
// Status exception bits are sticky until FNCLEX clears them.
inline bool Raise(X86::Cpu *cpu, unsigned flags)
{
    unsigned status = 0;
    if (cpu->Get(X86::Reg::Fpsw, status) != X86::CpuError::Ok)
        return false;
    status = (uint16_t)(status | (flags & 0x3f));
    return cpu->Set(X86::Reg::Fpsw, status) == X86::CpuError::Ok;
}
// Subfunction 10 counts occupied registers from the eight two-bit tag fields.
inline bool Depth(X86::Cpu *cpu, unsigned &count)
{
    unsigned top = 0, tags = 0, status = 0;
    if (!Slot(cpu, top, tags, status))
        return false;
    count = 0;
    for (unsigned i = 0; i < 8; ++i)
        if (((tags >> (i * 2)) & 3) != 3)
            ++count;
    return true;
}
} // namespace Win98Win87Em
