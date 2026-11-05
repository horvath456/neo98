// SPDX-License-Identifier: GPL-2.0-or-later
// Releasing translated code must not scan FEX's whole cache for each page.
#include "../src/x86_cpu.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace X86;
static void okay(unsigned error) { assert(error == CpuError::Ok); }
static constexpr unsigned Code = 0x100000, Back = 0x20000, Stack = 0x30000, Pages = 2048;
static unsigned Call(Cpu &cpu, unsigned address)
{
    const unsigned esp = Stack + 0x3ff0;
    okay(cpu.Write(esp, &Back, 4)); okay(cpu.Set(Reg::Esp, esp));
    okay(cpu.Run(address, Back, 0));
    unsigned value = 0; okay(cpu.Get(Reg::Eax, value)); return value;
}
static void Body(Cpu &cpu, unsigned address, unsigned value)
{
    unsigned char code[] = {0xb8, 0, 0, 0, 0, 0xc3};
    memcpy(code + 1, &value, 4); okay(cpu.Write(address, code, sizeof code));
}
int main()
{
    Cpu *cpu = nullptr; okay(CreateCpu(CpuBackend::Fex, cpu));
    okay(cpu->Map(Code, Pages * 4096, Prot::All));
    okay(cpu->Map(Back, 0x4000, Prot::All));
    okay(cpu->Map(Stack, 0x4000, Prot::Read | Prot::Write));
    const unsigned char nop = 0x90; okay(cpu->Write(Back, &nop, 1));
    for (unsigned n = 0; n < Pages; ++n) Body(*cpu, Code + n * 4096, n);
    for (unsigned n = 0; n < Pages; ++n)
        if (n != Pages / 2) assert(Call(*cpu, Code + n * 4096) == n);
    // One uncompiled page splits the translated code into two ranges.
    const auto start = std::chrono::steady_clock::now();
    okay(cpu->Unmap(Code + 4096, (Pages - 2) * 4096));
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    assert(milliseconds < 250); // Previously about 588 ms for 2048 pages.
    unsigned value = 0;
    assert(cpu->Read(Code + 4096, &value, 4) == CpuError::ReadUnmapped);
    assert(Call(*cpu, Code) == 0);
    assert(Call(*cpu, Code + (Pages - 1) * 4096) == Pages - 1);
    okay(cpu->Map(Code + 4096, (Pages - 2) * 4096, Prot::All));
    Body(*cpu, Code + 4096, 0x12345678);
    assert(Call(*cpu, Code + 4096) == 0x12345678);
    // FlushInstructionCache also groups ranges and rereads host-written code.
    auto *bytes = static_cast<unsigned char *>(cpu->HostPointer(Code, 4096));
    assert(bytes); value = 0x87654321; memcpy(bytes + 1, &value, 4);
    okay(cpu->FlushCode()); assert(Call(*cpu, Code) == value);
    assert(Call(*cpu, Code + 4096) == 0x12345678);
    okay(cpu->Unmap(Code, Pages * 4096)); delete cpu;
    printf("CPU CODE RELEASE PASS: code gaps, surviving translations, reuse, flush; %.3f ms\n", milliseconds);
}
