// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-Z's VIA ALTINST probe must stay a guest #UD. FEX's Linux-only thunk
// and callback opcodes must never read host pointers or leave the dispatcher.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace X86;
static void okay(unsigned e) { assert(e == CpuError::Ok); }
int main()
{
    Cpu *cpu = nullptr;
    okay(CreateCpu(ProcessCpuBackend(), cpu));
    okay(cpu->Map(0x400000, 0x1000, Prot::All));
    okay(cpu->Map(0x600000, 0x1000, Prot::Read | Prot::Write));
    unsigned checks = 0;
    for (unsigned opcode : {0x3eu, 0x3fu})
        for (bool prefix : {false, true})
            for (bool boundary : {false, true})
            {
                unsigned char code[3] = {0x66, 0x0f, (unsigned char)opcode};
                const unsigned length = prefix ? 3 : 2;
                const unsigned address = boundary ? 0x401000 - length : 0x400040;
                okay(cpu->Write(address, code + (prefix ? 0 : 1), length));
                okay(cpu->InvalidateCode(0x400000, 0x1000));
                okay(cpu->Set(Reg::Esp, 0x600800));
                const unsigned error = cpu->Run(address, address + length, 100);
                unsigned rip = 0, sp = 0;
                okay(cpu->Get(Reg::Eip, rip)); okay(cpu->Get(Reg::Esp, sp));
                printf("reserved opcode=%02x prefix=%u boundary=%u error=%u eip=%x esp=%x\n",
                       opcode, prefix, boundary, error, rip, sp);
                fflush(stdout);
                assert(error == CpuError::InvalidInstruction && rip == address && sp == 0x600800);
                // The engine still executes ordinary code after that fault.
                const unsigned char good[] = {0xb8, 0x78, 0x56, 0x34, 0x12};
                okay(cpu->Write(0x400100, good, sizeof good));
                okay(cpu->InvalidateCode(0x400000, 0x1000));
                okay(cpu->Run(0x400100, 0x400105, 100));
                unsigned eax = 0; okay(cpu->Get(Reg::Eax, eax)); assert(eax == 0x12345678);
                ++checks;
            }
    printf("CPU RESERVED OPCODES PASS checks=%u\n", checks);
    delete cpu;
}
