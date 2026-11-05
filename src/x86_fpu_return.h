// SPDX-License-Identifier: GPL-2.0-or-later
// Native x86 ABI provider, native host code.
#pragma once
#include <stdint.h>
#include "x86_cpu.h"
namespace X86FpuReturn {
// Unicorn's raw register reset marks all x87 slots occupied and leaves both
// control words zero. A fresh native thread needs an independent usable FPU:
// empty registers, zero status, all tags empty, the given control word. A
// Win32 thread gets 027Fh, the state KERNEL32's x87 emulator gives a thread
// on its first x87 instruction: reset, then load the control word 027Fh.
// CRT startup may change it afterwards.
inline unsigned InitializeThread(X86::Cpu *cpu, uint16_t control = 0x037f)
{
    using X86::Reg;
    using X86::CpuError;
    const uint16_t status = 0, tags = 0xffff;
    const uint32_t mxcsr = 0x1f80;
    const uint64_t empty[2] = {};
    unsigned error;
    for (unsigned i = 0; i < 8; ++i) {
        error = cpu->SetWide(Reg::Fp0 + i, empty);
        if (error != CpuError::Ok) return error;
        error = cpu->SetWide(Reg::Xmm0 + i, empty);
        if (error != CpuError::Ok) return error;
    }
    error = cpu->Set(Reg::Fpcw, control);
    if (error != CpuError::Ok) return error;
    error = cpu->Set(Reg::Fpsw, status);
    if (error != CpuError::Ok) return error;
    error = cpu->Set(Reg::Fptag, tags);
    if (error != CpuError::Ok) return error;
    return cpu->Set(Reg::Mxcsr, mxcsr);
}
// Push an already rounded binary64 result onto the guest's x87 stack. Unicorn
// FP0..7 are physical ten-byte registers; FPSW.TOP selects logical ST(0).
// No host floating arithmetic or host long-double representation is involved.
inline unsigned Push64(X86::Cpu *cpu, uint64_t bits)
{
    using X86::Reg;
    using X86::CpuError;
    const unsigned exponent = unsigned(bits >> 52) & 2047;
    const uint64_t fraction = bits & UINT64_C(0x000fffffffffffff);
    // Signalling-NaN exception delivery needs a separate FPU exception provider.
    if (exponent == 2047 && fraction && !(fraction & (UINT64_C(1) << 51)))
        return CpuError::Argument;
    unsigned status = 0, tags = 0;
    unsigned error = cpu->Get(Reg::Fpsw, status);
    if (error != CpuError::Ok) return error;
    error = cpu->Get(Reg::Fptag, tags);
    if (error != CpuError::Ok) return error;
    const unsigned top = ((status >> 11) - 1) & 7, shift = top * 2;
    if (((tags >> shift) & 3) != 3) return CpuError::Exception;
    uint64_t mantissa = 0;
    unsigned extended_exponent = 0;
    if (exponent) {
        mantissa = (fraction | (UINT64_C(1) << 52)) << 11;
        extended_exponent = exponent == 2047 ? 32767 : exponent + 15360;
    } else if (fraction) {
        unsigned highest = 0;
        for (uint64_t value = fraction; value >>= 1;) ++highest;
        mantissa = fraction << (63 - highest);
        extended_exponent = 15309 + highest;
    }
    // The mantissa and then sign and exponent, the layout of Reg::Fp0.
    uint64_t raw[2] = {mantissa, extended_exponent | ((bits >> 48) & 0x8000)};
    error = cpu->SetWide(Reg::Fp0 + top, raw);
    if (error != CpuError::Ok) return error;
    tags = uint16_t(tags & ~(3u << shift));
    error = cpu->Set(Reg::Fptag, tags);
    if (error != CpuError::Ok) return error;
    status = uint16_t((status & ~0x3800u) | (top << 11));
    return cpu->Set(Reg::Fpsw, status);
}
}
