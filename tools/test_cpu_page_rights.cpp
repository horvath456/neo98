// SPDX-License-Identifier: GPL-2.0-or-later
// Page rights per 4 KiB guest page under X86::Cpu, whatever the host page size:
// four guest pages of one 16 KiB host page with different rights,
// read and written by guest instructions. Runs under the engine NEO98_CPU
// selects. Build: tools/test_cpu_page_rights.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using X86::CpuError;
using X86::CpuFault;
using X86::Prot;
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
// mov eax,[address] (A1) or mov [address],eax (A3); answers the run's error.
static unsigned Access(X86::Cpu *cpu, bool write, unsigned address, unsigned &eax)
{
    unsigned char code[5] = {(unsigned char)(write ? 0xa3 : 0xa1)};
    memcpy(code + 1, &address, 4);
    okay(cpu->Write(0x1000, code, sizeof code));
    okay(cpu->InvalidateCode(0x1000, 0x1000));
    okay(cpu->Set(X86::Reg::Eax, eax));
    const unsigned error = cpu->Run(0x1000, 0x1005, 100);
    cpu->Get(X86::Reg::Eax, eax);
    return error;
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    Seen seen;
    okay(cpu->SetFaultHook(Fault, &seen));
    okay(cpu->Map(0x1000, 0x1000, Prot::All));
    // One 16 KiB host page: read/write, read-only, no access, read/write.
    okay(cpu->Map(0x10000, 0x1000, Prot::Read | Prot::Write));
    okay(cpu->Map(0x11000, 0x1000, Prot::Read));
    okay(cpu->Map(0x12000, 0x1000, Prot::None));
    okay(cpu->Map(0x13000, 0x1000, Prot::Read | Prot::Write));
    const unsigned marks[4] = {0x11111111, 0x22222222, 0x33333333, 0x44444444};
    for (unsigned i = 0; i < 4; ++i)
        okay(cpu->Write(0x10000 + i * 0x1000 + 0x10, &marks[i], 4));
    unsigned checks = 0, eax = 0;
    // Reads of the readable pages see the host's bytes.
    for (unsigned i : {0u, 1u, 3u})
    {
        eax = 0;
        okay(Access(cpu, false, 0x10010 + i * 0x1000, eax));
        assert(eax == marks[i]);
        ++checks;
    }
    // Writes of the writable pages reach the host's view.
    for (unsigned i : {0u, 3u})
    {
        eax = 0xabcd0000 + i;
        okay(Access(cpu, true, 0x10020 + i * 0x1000, eax));
        unsigned back = 0;
        okay(cpu->Read(0x10020 + i * 0x1000, &back, 4));
        assert(back == 0xabcd0000 + i);
        ++checks;
    }
    // The read-only page refuses a write, the page without access a read and a
    // write, each at its own address; the bytes stay.
    struct Refused
    {
        bool write;
        unsigned address, kind;
    } refused[] = {{true, 0x11010, CpuFault::WriteProtected},
                   {false, 0x12010, CpuFault::ReadProtected},
                   {true, 0x12010, CpuFault::WriteProtected}};
    for (const auto &r : refused)
    {
        seen = {};
        eax = 0x5a5a5a5a;
        const unsigned error = Access(cpu, r.write, r.address, eax);
        assert(error != CpuError::Ok && X86::CpuMemoryError(error));
        assert(seen.count == 1 && seen.kind == r.kind && seen.address == r.address);
        unsigned back = 0;
        okay(cpu->Read(r.address, &back, 4));
        assert(back == marks[(r.address - 0x10000) >> 12]);
        ++checks;
    }
    // Protect changes one guest page only.
    okay(cpu->Protect(0x11000, 0x1000, Prot::Read | Prot::Write));
    okay(cpu->Protect(0x10000, 0x1000, Prot::Read));
    eax = 0x77777777;
    okay(Access(cpu, true, 0x11030, eax));
    seen = {};
    assert(Access(cpu, true, 0x10030, eax) != CpuError::Ok && seen.kind == CpuFault::WriteProtected &&
           seen.address == 0x10030);
    eax = 0;
    okay(Access(cpu, false, 0x10010, eax));
    assert(eax == marks[0]);
    checks += 3;
    printf("CPU PAGE RIGHTS PASS engine=%s checks=%u\n",
           cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn", checks);
    delete cpu;
    return 0;
}
