// SPDX-License-Identifier: GPL-2.0-or-later
// a block crossing a guest page must learn checked accesses.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }

int main(int argc, char **argv)
{
    assert(argc == 3);
    const unsigned offset = strtoul(argv[1], nullptr, 0);
    const bool write = atoi(argv[2]) != 0;
    constexpr unsigned Code = 0x10000, Data = 0x20000, Stack = 0x40000, Back = 0x50000;
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(Code, 0x8000, Prot::All));
    // The other 4-KiB guest pages are unmapped: this 16-KiB host page
    // refuses accesses in the window, but Data is valid in the direct view.
    okay(cpu->Map(Data, 0x1000, Prot::Read | Prot::Write));
    okay(cpu->Map(Stack, 0x4000, Prot::Read | Prot::Write));
    okay(cpu->Map(Back, 0x4000, Prot::All));
    const unsigned char nop = 0x90;
    okay(cpu->Write(Back, &nop, 1));
    std::vector<unsigned char> code(16, nop);
    code.push_back(write ? 0xa3 : 0xa1); // mov [Data],eax / mov eax,[Data]
    for (unsigned i = 0; i < 4; ++i) code.push_back(Data >> (i * 8));
    code.push_back(0xc3);
    if (offset == 0x2100)
    {
        // A multiblock entered on a later page, branching to earlier code.
        const unsigned target = Code + 0x1100;
        okay(cpu->Write(target, code.data(), code.size()));
        unsigned char jump[] = {0xe9, 0, 0, 0, 0};
        const unsigned relative = target - (Code + offset + sizeof jump);
        memcpy(jump + 1, &relative, 4);
        okay(cpu->Write(Code + offset, jump, sizeof jump));
    }
    else okay(cpu->Write(Code + offset, code.data(), code.size()));
    const unsigned initial = 0x12345678;
    okay(cpu->Write(Data, &initial, 4));
    okay(cpu->FlushCode());
    for (unsigned round = 0; round < 256; ++round)
    {
        const unsigned esp = Stack + 0x3ff0;
        okay(cpu->Write(esp, &Back, 4));
        okay(cpu->Set(Reg::Esp, esp));
        okay(cpu->Set(Reg::Eax, round));
        okay(cpu->Run(Code + offset, Back, 0));
        unsigned value = 0;
        if (write) okay(cpu->Read(Data, &value, 4));
        else okay(cpu->Get(Reg::Eax, value));
        assert(value == (write ? round : initial));
    }
    printf("CPU CODE PLACEMENT PASS offset=%u write=%u rounds=256\n", offset, write);
    delete cpu; // NEO98_FEX_DEBUG reports the actual emulated accesses.
}
