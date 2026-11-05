// SPDX-License-Identifier: GPL-2.0-or-later
// Loading a segment register uses only the low sixteen bits of the source
// register. The Galaxy sound system used by Moorhuhn leaves a processor
// signature in the upper half, which must be ignored. Run with the backend
// selected by NEO98_CPU.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using X86::CpuError;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
struct Seen
{
    unsigned kind = 0, address = 0, count = 0;
};
static bool Fault(X86::Cpu &, unsigned kind, unsigned address, unsigned, void *context)
{
    auto &seen = *static_cast<Seen *>(context);
    seen.kind = kind;
    seen.address = address;
    ++seen.count;
    return false;
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    Seen seen;
    okay(cpu->SetFaultHook(Fault, &seen));
    // The descriptor table of a Win32 process of this port (src/x86_app.cpp):
    // one 16 KiB block holding null, flat-code and flat-data descriptors.
    const unsigned gdt_base = 0x7ffdc000;
    okay(cpu->Map(gdt_base, 0x4000, X86::Prot::Read | X86::Prot::Write));
    const unsigned long long descriptors[] = {0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull};
    okay(cpu->Write(gdt_base, descriptors, sizeof descriptors));
    X86::DescriptorTable gdt;
    gdt.base = gdt_base;
    gdt.limit = 3 * 8 - 1;
    okay(cpu->SetWide(X86::Reg::Gdtr, &gdt));
    okay(cpu->Set(X86::Reg::Cr0, 1));
    okay(cpu->Set(X86::Reg::Cs, 8));
    for (int reg : {X86::Reg::Ds, X86::Reg::Es, X86::Reg::Ss})
        okay(cpu->Set(reg, 0x10));
    okay(cpu->Map(0x1000, 0x1000, X86::Prot::All));
    okay(cpu->Map(0x3000, 0x1000, X86::Prot::Read | X86::Prot::Write));
    // mov eax,0x00010010 / mov es,eax / mov edi,0x3000 / mov eax,0x12345678 /
    // stosd / mov ecx,es / mov eax,0x00020010 / push eax / pop ds (low word) /
    // hlt-free end at 0x101c.
    const unsigned char code[] = {
        0xb8, 0x10, 0x00, 0x01, 0x00,       // mov eax,0x00010010
        0x8e, 0xc0,                         // mov es,eax
        0xbf, 0x00, 0x30, 0x00, 0x00,       // mov edi,0x3000
        0xb8, 0x78, 0x56, 0x34, 0x12,       // mov eax,0x12345678
        0xab,                               // stosd
        0x8c, 0xc1,                         // mov ecx,es
        0xba, 0x10, 0x00, 0x02, 0x00,       // mov edx,0x00020010
        0x8e, 0xda,                         // mov ds,edx
        0x8c, 0xdb,                         // mov ebx,ds
    };
    okay(cpu->Write(0x1000, code, sizeof code));
    okay(cpu->InvalidateCode(0x1000, 0x1000));
    okay(cpu->Set(X86::Reg::Esp, 0x3800));
    const unsigned error = cpu->Run(0x1000, 0x1000 + sizeof code, 100);
    if (error != CpuError::Ok)
    {
        printf("CPU SEGMENT LOAD FAILED error=%u fault kind=%u address=%08x\n", error, seen.kind, seen.address);
        return 1;
    }
    unsigned es = 0, ecx = 0, ebx = 0, stored = 0;
    cpu->Get(X86::Reg::Es, es);
    cpu->Get(X86::Reg::Ecx, ecx);
    cpu->Get(X86::Reg::Ebx, ebx);
    okay(cpu->Read(0x3000, &stored, 4));
    assert(seen.count == 0);
    assert((es & 0xffff) == 0x10 && (ecx & 0xffff) == 0x10 && (ebx & 0xffff) == 0x10);
    assert(stored == 0x12345678);
    printf("CPU SEGMENT LOAD PASS engine=%s\n", cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete cpu;
    return 0;
}
