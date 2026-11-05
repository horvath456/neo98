// SPDX-License-Identifier: GPL-2.0-or-later
// BOUND r32,m32&32 (62 /r): inside the bounds nothing happens, outside the
// guest gets interrupt 5 with EIP at the instruction. Delphi and Turbo Pascal
// compile it around every array index with {$R+}; WinPac 2 runs one in its
// first form. FEXCore has no BOUND, tools/host_fex_cpu.cpp carries
// it out from the invalid-opcode break. Runs under the engine NEO98_CPU
// selects. Build: tools/test_cpu_bound_native.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using X86::CpuError;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
struct Seen
{
    unsigned number = 0, eip = 0, count = 0;
};
// The handler of the test widens the upper bound, as a debugger would; the
// instruction then runs again and passes.
static void Interrupt(X86::Cpu &cpu, unsigned number, void *context)
{
    auto &seen = *static_cast<Seen *>(context);
    seen.number = number;
    cpu.Get(X86::Reg::Eip, seen.eip);
    if (++seen.count > 3)
        return;
    const int upper = 100;
    cpu.Write(0x3004, &upper, 4);
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    Seen seen;
    okay(cpu->SetInterruptHook(Interrupt, &seen));
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
    // 3000: bounds 0..6 (dwords); 3010: bounds -2..2 (words); 3028: 5..9.
    const int wide[] = {0, 6};
    const short narrow[] = {-2, 2};
    const int indexed[] = {5, 9};
    okay(cpu->Write(0x3000, wide, sizeof wide));
    okay(cpu->Write(0x3010, narrow, sizeof narrow));
    okay(cpu->Write(0x3028, indexed, sizeof indexed));
    const unsigned char code[] = {
        0xb8, 0x03, 0x00, 0x00, 0x00,             // 1000 mov eax,3
        0x62, 0x05, 0x00, 0x30, 0x00, 0x00,       // 1005 bound eax,[3000] inside
        0xbb, 0x11, 0x00, 0x00, 0x00,             // 100b mov ebx,11h
        0xb8, 0xff, 0xff, 0x00, 0x00,             // 1010 mov eax,0000ffffh ax = -1
        0x66, 0x62, 0x05, 0x10, 0x30, 0x00, 0x00, // 1015 bound ax,[3010] inside
        0xbe, 0x00, 0x30, 0x00, 0x00,             // 101c mov esi,3000h
        0xb9, 0x08, 0x00, 0x00, 0x00,             // 1021 mov ecx,8
        0xba, 0x09, 0x00, 0x00, 0x00,             // 1026 mov edx,9
        0x62, 0x54, 0x8e, 0x08,                   // 102b bound edx,[esi+ecx*4+8] inside
        0xbf, 0x22, 0x00, 0x00, 0x00,             // 102f mov edi,22h
        0xb8, 0x07, 0x00, 0x00, 0x00,             // 1034 mov eax,7
        0x62, 0x06,                               // 1039 bound eax,[esi] outside
        0xbd, 0x33, 0x00, 0x00, 0x00,             // 103b mov ebp,33h
    };
    okay(cpu->Write(0x1000, code, sizeof code));
    okay(cpu->InvalidateCode(0x1000, 0x1000));
    okay(cpu->Set(X86::Reg::Esp, 0x3800));
    const unsigned error = cpu->Run(0x1000, 0x1000 + sizeof code, 1000);
    unsigned ebx = 0, edi = 0, ebp = 0;
    cpu->Get(X86::Reg::Ebx, ebx);
    cpu->Get(X86::Reg::Edi, edi);
    cpu->Get(X86::Reg::Ebp, ebp);
    if (error != CpuError::Ok || ebx != 0x11 || edi != 0x22 || ebp != 0x33 || seen.count != 1 || seen.number != 5 ||
        seen.eip != 0x1039)
    {
        printf("CPU BOUND FAILED error=%u ebx=%x edi=%x ebp=%x interrupts=%u number=%u eip=%08x\n", error, ebx, edi,
               ebp, seen.count, seen.number, seen.eip);
        return 1;
    }
    printf("CPU BOUND PASS engine=%s\n", cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete cpu;
    return 0;
}
