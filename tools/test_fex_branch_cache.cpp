// SPDX-License-Identifier: GPL-2.0-or-later
// indirect targets with colliding low bits survive patch/unmap/remap.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using X86::Reg;
using X86::Prot;
static void okay(unsigned v) { assert(v == X86::CpuError::Ok); }
int main(int argc, char **argv)
{
    assert(argc == 2);
    const unsigned stride = strtoul(argv[1], nullptr, 0);
    constexpr unsigned Code = 0x1000000, Entry = 0x2000000, Stack = 0x2100000, Back = Entry + 0x100;
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(Code, 0x800000, Prot::All));
    okay(cpu->Map(Entry, 0x4000, Prot::All));
    okay(cpu->Map(Stack, 0x4000, Prot::Read | Prot::Write));
    const unsigned char indirect[] = {0xff, 0xe6}; // jmp esi
    const unsigned char nop = 0x90;
    okay(cpu->Write(Entry, indirect, sizeof indirect));
    okay(cpu->Write(Back, &nop, 1));
    auto install = [&](unsigned at, unsigned value) {
        unsigned char body[] = {0xb8, 0, 0, 0, 0, 0xc3};
        memcpy(body + 1, &value, 4);
        okay(cpu->Write(at, body, sizeof body));
        okay(cpu->InvalidateCode(at, sizeof body));
    };
    auto run = [&](unsigned at, unsigned expected, unsigned error = X86::CpuError::Ok) {
        const unsigned esp = Stack + 0x3ff0;
        okay(cpu->Write(esp, &Back, 4));
        okay(cpu->Set(Reg::Esp, esp));
        okay(cpu->Set(Reg::Esi, at));
        assert(cpu->Run(Entry, Back, 0) == error);
        if (!error) {
            unsigned result = 0;
            okay(cpu->Get(Reg::Eax, result));
            assert(result == expected);
        }
    };
    for (unsigned i = 0; i < 8; ++i) install(Code + i * stride, i);
    for (unsigned round = 0; round < 4; ++round)
        for (unsigned i = 0; i < 8; ++i) run(Code + i * stride, i);
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned at = Code + i * stride;
        install(at, i + 100);
        run(at, i + 100);
        okay(cpu->Unmap(at, 0x1000));
        run(at, 0, X86::CpuError::FetchUnmapped);
        okay(cpu->Map(at, 0x1000, Prot::All));
        install(at, i + 200);
        run(at, i + 200);
    }
    okay(cpu->FlushCode());
    for (unsigned i = 0; i < 8; ++i) run(Code + i * stride, i + 200);
    printf("CPU BRANCH CACHE PASS stride=%u checks=64\n", stride);
    delete cpu;
}
