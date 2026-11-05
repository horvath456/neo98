// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "x86_cpu.h"
namespace X86 {
struct CpuPortableRegisters
{
    unsigned words[X86::Reg::Dr7 + 1] = {};
    unsigned char fp[8][16] = {}, xmm[8][16] = {};
    X86::DescriptorTable gdtr, ldtr;
};
inline void ReadCpuPortableRegisters(X86::Cpu &cpu, CpuPortableRegisters &r)
{
    for (int reg = 0; reg <= X86::Reg::Dr7; ++reg)
        cpu.Get(reg, r.words[reg]);
    for (int i = 0; i < 8; ++i)
    {
        cpu.GetWide(X86::Reg::Fp0 + i, r.fp[i]);
        cpu.GetWide(X86::Reg::Xmm0 + i, r.xmm[i]);
    }
    cpu.GetWide(X86::Reg::Gdtr, &r.gdtr);
    cpu.GetWide(X86::Reg::Ldtr, &r.ldtr);
}
inline void WriteCpuPortableRegisters(X86::Cpu &cpu, const CpuPortableRegisters &r)
{
    using X86::Reg;
    // The tables first: loading a segment register reads its descriptor.
    cpu.SetWide(Reg::Gdtr, &r.gdtr);
    cpu.SetWide(Reg::Ldtr, &r.ldtr);
    cpu.Set(Reg::Cr0, r.words[Reg::Cr0]);
    const int segments[] = {Reg::Es, Reg::Cs, Reg::Ss, Reg::Ds, Reg::Fs, Reg::Gs};
    for (int reg : segments)
        cpu.Set(reg, r.words[reg]);
    for (int reg = Reg::Eax; reg <= Reg::Edi; ++reg)
        cpu.Set(reg, r.words[reg]);
    cpu.Set(Reg::Eip, r.words[Reg::Eip]);
    cpu.Set(Reg::Eflags, r.words[Reg::Eflags]);
    // The control word before the registers: with 64-bit precision the
    // engine changes the form it keeps them in.
    cpu.Set(Reg::Fpcw, r.words[Reg::Fpcw]);
    cpu.Set(Reg::Fpsw, r.words[Reg::Fpsw]);
    cpu.Set(Reg::Fptag, r.words[Reg::Fptag]);
    for (int i = 0; i < 8; ++i)
        cpu.SetWide(Reg::Fp0 + i, r.fp[i]);
    const int pointers[] = {Reg::Fip, Reg::Fcs, Reg::Fdp, Reg::Fds, Reg::Fop, Reg::Mxcsr};
    for (int reg : pointers)
        cpu.Set(reg, r.words[reg]);
    for (int i = 0; i < 8; ++i)
        cpu.SetWide(Reg::Xmm0 + i, r.xmm[i]);
    const int debug[] = {Reg::Dr0, Reg::Dr1, Reg::Dr2, Reg::Dr3, Reg::Dr6, Reg::Dr7};
    for (int reg : debug)
        cpu.Set(reg, r.words[reg]);
}
} // namespace X86
