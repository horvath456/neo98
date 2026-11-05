// SPDX-License-Identifier: GPL-2.0-or-later
// Code on a host page that turns into a checked page stays current.
// Under FEXCore a 16-KiB host page holds four guest pages. A guest page with
// translated code is write-protected; the second guest write into code of
// that host page switches it to blocks that check their own bytes and leaves
// it writable (FexCpu::BeforeGuestWrite). The translations of the other
// guest pages of the host page were made without that check; they have to
// go at the switch, or a later guest write into them is never seen and the
// old code keeps running. Unicorn sees every store into translated code.
// Build: tools/test_cpu_mixed_page.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
// A and B share the host page 10000h..13FFFh; the writers and the return
// point lie on other host pages.
static constexpr unsigned A = 0x10000, B = 0x11000, Writers = 0x20000, Back = 0x30000, Stack = 0x40000;
static unsigned Call(X86::Cpu &cpu, unsigned function)
{
    const unsigned esp = Stack + 0xff00;
    okay(cpu.Write(esp, &Back, 4));
    okay(cpu.Set(Reg::Esp, esp));
    okay(cpu.Set(Reg::Eax, 0));
    okay(cpu.Run(function, Back, 0));
    unsigned eax = 0;
    okay(cpu.Get(Reg::Eax, eax));
    return eax;
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    const unsigned bases[] = {A, Writers, Back};
    for (unsigned base : bases)
        okay(cpu->Map(base, 0x4000, Prot::All));
    okay(cpu->Map(Stack, 0x10000, Prot::Read | Prot::Write));
    const unsigned char a[] = {0xb8, 0x0a, 0, 0, 0, 0xc3};   // mov eax, 10; ret
    const unsigned char b[] = {0xb8, 0x01, 0, 0, 0, 0xc3};   // mov eax, 1; ret
    // mov byte [A+800h], 90h; ret and mov byte [B+1], 2; ret
    const unsigned char writers[] = {0xc6, 0x05, 0x00, 0x08, 0x01, 0x00, 0x90, 0xc3,
                                     0xc6, 0x05, 0x01, 0x10, 0x01, 0x00, 0x02, 0xc3};
    const unsigned char nop = 0x90;
    okay(cpu->Write(A, a, sizeof a));
    okay(cpu->Write(B, b, sizeof b));
    okay(cpu->Write(Writers, writers, sizeof writers));
    okay(cpu->Write(Back, &nop, 1));
    okay(cpu->FlushCode());
    // B is translated while its host page still catches writes.
    assert(Call(*cpu, B) == 1);
    // Two guest writes into A, each after A ran: the host page turns checked.
    for (int round = 0; round < 2; ++round)
    {
        assert(Call(*cpu, A) == 10);
        Call(*cpu, Writers);
    }
    // The guest changes B's immediate; B must return the new value.
    Call(*cpu, Writers + 8);
    const unsigned result = Call(*cpu, B);
    printf("b=%u\n", result);
    assert(result == 2);
    printf("CPU MIXED PAGE PASS engine=%s\n", cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete cpu;
    return 0;
}
