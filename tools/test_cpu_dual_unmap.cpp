// SPDX-License-Identifier: GPL-2.0-or-later
// Large heap decommits must not repeatedly split Unicorn's mirrored regions.
// Exercise both engines, rights boundaries, surviving bytes and address reuse.
#include "../src/x86_cpu.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
using namespace X86;
static void okay(unsigned error) { assert(error == CpuError::Ok); }
static unsigned Read16(Cpu &cpu, unsigned address, unsigned &result)
{
    // 16-bit code and flat data selector: MOV EAX, moffs32.
    unsigned char code[] = {0x66, 0x67, 0xa1, 0, 0, 0, 0};
    memcpy(code + 3, &address, 4);
    okay(cpu.Write(0x1000, code, sizeof code));
    okay(cpu.Set(Reg::Eip, 0x1000));
    const unsigned error = cpu.Run(0x1000, 0x1000 + sizeof code, 10);
    if (!error) okay(cpu.Get(Reg::Eax, result));
    return error;
}
int main()
{
    Cpu *main = nullptr;
    okay(CreateCpu(CpuBackend::Fex, main));
    Cpu *cpu = CreateDualCpu(main);
    okay(cpu->Map(0x4000, 0x4000, Prot::All));
    const unsigned long long gdt[] = {0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull};
    const unsigned long long ldt[] = {0, 0x00009a000000ffffull};
    okay(cpu->Write(0x4000, gdt, sizeof gdt));
    okay(cpu->Write(0x5000, ldt, sizeof ldt));
    const DescriptorTable gd{0x4000, 23}, ld{0x5000, 15, 0x18, 0x82};
    okay(cpu->SetWide(Reg::Gdtr, &gd));
    okay(cpu->SetWide(Reg::Ldtr, &ld));
    okay(cpu->Set(Reg::Cr0, 1));
    for (int reg : {Reg::Ds, Reg::Es, Reg::Ss}) okay(cpu->Set(reg, 0x10));
    okay(cpu->Map(0x1000, 0x1000, Prot::All));
    okay(cpu->Map(0x2000000, 0x1000000, Prot::Read | Prot::Write));
    // Private neighbours of shared backing have no contiguous HostPointer.
    // They are deliberately absent from the second engine when it starts.
    void *shared = nullptr;
    assert(posix_memalign(&shared, 0x4000, 0x4000) == 0);
    memset(shared, 0, 0x4000);
    okay(cpu->MapHost(0x5000000, 0x1000, Prot::Read | Prot::Write, shared));
    okay(cpu->Map(0x5001000, 0x7000, Prot::Read | Prot::Write));
    assert(main->HostPointer(0x5001000, 0x1000) == nullptr);
    CpuContext *flat = nullptr;
    okay(cpu->NewContext(flat));
    okay(cpu->Save(flat));
    okay(cpu->Set(Reg::Cs, 0xc));
    const unsigned marker = 0x12345678;
    for (unsigned at : {0x2000000u, 0x2800000u, 0x2fffffcu}) okay(cpu->Write(at, &marker, 4));
    unsigned result = 0;
    okay(Read16(*cpu, 0x2800000, result));
    assert(result == marker);
    // The secondary engine has a register codec: live 16-bit registers can
    // be saved too (9d4a3a5f), so neither the live state nor its context refuses.
    assert(cpu->StateRefusal() == nullptr);
    CpuContext *second_context = nullptr;
    okay(cpu->NewContext(second_context));
    okay(cpu->Save(second_context));
    assert(cpu->ContextStateRefusal(second_context) == nullptr);
    okay(cpu->Restore(flat));
    assert(cpu->StateRefusal() == nullptr); // the second engine now only caches shared memory
    assert(cpu->ContextStateRefusal(flat) == nullptr);
    assert(cpu->ContextStateRefusal(second_context) == nullptr); // the saved 16-bit callback stays exportable
    cpu->FreeContext(second_context);
    cpu->FreeContext(flat);
    okay(cpu->Set(Reg::Cs, 0xc));
    okay(cpu->Unmap(0x5000000, 0x8000));
    free(shared);
    okay(cpu->Map(0x5000000, 0x8000, Prot::Read | Prot::Write));
    okay(cpu->Write(0x5001000, &marker, 4));
    okay(Read16(*cpu, 0x5001000, result)); assert(result == marker);
    okay(cpu->Unmap(0x5000000, 0x8000));
    okay(cpu->Protect(0x2800000, 0x1000, Prot::Read));
    const auto start = std::chrono::steady_clock::now();
    okay(cpu->Unmap(0x2100000, 0xe00000));
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    assert(milliseconds < 250); // Previously thousands of individual region splits.
    assert(cpu->Read(0x2800000, &result, 4) == CpuError::ReadUnmapped);
    assert(Read16(*cpu, 0x2800000, result) == CpuError::ReadUnmapped);
    for (unsigned at : {0x2000000u, 0x2fffffcu}) {
        okay(cpu->Read(at, &result, 4)); assert(result == marker);
        okay(Read16(*cpu, at, result)); assert(result == marker);
    }
    okay(cpu->Map(0x2100000, 0xe00000, Prot::Read | Prot::Write));
    const unsigned replacement = 0x87654321;
    okay(cpu->Write(0x2800000, &replacement, 4));
    okay(Read16(*cpu, 0x2800000, result)); assert(result == replacement);
    okay(cpu->Unmap(0x2000000, 0x1000000));
    delete cpu;
    printf("CPU DUAL UNMAP PASS: both engines, mirror gaps, permissions, surviving bytes, reuse; 14 MiB unmap %.2f ms\n", milliseconds);
}
