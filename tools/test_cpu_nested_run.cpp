// SPDX-License-Identifier: GPL-2.0-or-later
// One processor runs inside the gate hook of another, the way the TrueType
// machine (src/truetype_machine.cpp) runs inside a GDI call of a process.
// The inner run ends at an address inside its own gate range, calls a gate
// of its own and changes the x87 rounding; the outer guest must go on with
// its own registers and its own rounding. Under FEXCore the rounding of a
// thread lives in the host FPCR.
// Build: tools/test_cpu_nested_native.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
static constexpr unsigned Code = 0x10000, Data = 0x20000, Stack = 0x30000, Gate = 0x50000, Rounds = 50;
static constexpr unsigned Return = Gate + 0xff0;
struct Seen
{
    X86::Cpu *inner = nullptr;
    unsigned outer_calls = 0, inner_calls = 0, inner_runs = 0;
};
static void InnerGate(X86::Cpu &, unsigned address, void *context)
{
    // The return address of the run: nothing to serve.
    if (address == Return)
        return;
    ++static_cast<Seen *>(context)->inner_calls;
}
static void OuterGate(X86::Cpu &cpu, unsigned, void *context)
{
    auto &seen = *static_cast<Seen *>(context);
    ++seen.outer_calls;
    X86::Cpu &inner = *seen.inner;
    const unsigned esp = Stack + 0xfff0 - 4;
    okay(inner.Write(esp, &Return, 4));
    okay(inner.Set(Reg::Esp, esp));
    okay(inner.Run(Code, Return, 0));
    unsigned eip = 0, down = 0, up = 0;
    okay(inner.Get(Reg::Eip, eip));
    assert(eip == Return);
    okay(inner.Read(Data + 0x30, &down, 4));
    okay(inner.Read(Data + 0x34, &up, 4));
    ++seen.inner_runs;
    okay(cpu.Set(Reg::Eax, down * 10 + up));
}
static void Map(X86::Cpu &cpu)
{
    okay(cpu.Map(Code, 0x10000, Prot::All));
    okay(cpu.Map(Data, 0x10000, Prot::Read | Prot::Write));
    okay(cpu.Map(Stack, 0x10000, Prot::Read | Prot::Write));
    okay(cpu.Map(Gate, 0x10000, Prot::All));
    unsigned char gate[0x1000];
    memset(gate, 0xc3, sizeof gate);
    memset(gate + 0xff0, 0, 16);
    okay(cpu.Write(Gate, gate, sizeof gate));
    okay(cpu.Set(Reg::Esp, Stack + 0xfff0));
}
static void Double(X86::Cpu &cpu, unsigned address, double value) { okay(cpu.Write(address, &value, 8)); }
int main()
{
    X86::Cpu *outer = nullptr, *inner = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), outer));
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), inner));
    Map(*outer);
    Map(*inner);
    const unsigned char outer_code[] = {
        0xb9, Rounds, 0x00, 0x00, 0x00,         // mov ecx, Rounds
        0xe8, 0xf6, 0xff, 0x03, 0x00,           // l: call 50000h (the gate)
        0xdd, 0x05, 0x20, 0x00, 0x02, 0x00,     // fld qword [20020h] (2.5)
        0xdb, 0x1d, 0x30, 0x00, 0x02, 0x00,     // fistp dword [20030h]
        0x01, 0x05, 0x00, 0x00, 0x02, 0x00,     // add [20000h], eax
        0xa1, 0x30, 0x00, 0x02, 0x00,           // mov eax, [20030h]
        0x01, 0x05, 0x04, 0x00, 0x02, 0x00,     // add [20004h], eax
        0x49,                                   // dec ecx
        0x75, 0xdb,                             // jnz l
        0x90};                                  // nop: the end of the Run
    const unsigned char inner_code[] = {
        0xdd, 0x05, 0x20, 0x00, 0x02, 0x00,     // fld qword [20020h] (2.7)
        0xdb, 0x1d, 0x30, 0x00, 0x02, 0x00,     // fistp dword [20030h]: rounds down, 2
        0xe8, 0xef, 0xff, 0x03, 0x00,           // call 50000h (the inner gate)
        0xd9, 0x2d, 0x40, 0x00, 0x02, 0x00,     // fldcw [20040h]: round up
        0xdd, 0x05, 0x28, 0x00, 0x02, 0x00,     // fld qword [20028h] (2.2)
        0xdb, 0x1d, 0x34, 0x00, 0x02, 0x00,     // fistp dword [20034h]: 3
        0xd9, 0x2d, 0x44, 0x00, 0x02, 0x00,     // fldcw [20044h]: round down again
        0xc3};                                  // ret to Return, inside the gate range
    okay(outer->Write(Code, outer_code, sizeof outer_code));
    okay(inner->Write(Code, inner_code, sizeof inner_code));
    Double(*outer, Data + 0x20, 2.5);
    Double(*inner, Data + 0x20, 2.7);
    Double(*inner, Data + 0x28, 2.2);
    const uint16_t up = 0x0a7f, down = 0x067f;
    okay(inner->Write(Data + 0x40, &up, 2));
    okay(inner->Write(Data + 0x44, &down, 2));
    okay(outer->Set(Reg::Fpcw, 0x027f));
    okay(inner->Set(Reg::Fpcw, down));
    Seen seen;
    seen.inner = inner;
    okay(outer->SetGateHook(Gate, Gate + 0xfff, OuterGate, &seen));
    okay(inner->SetGateHook(Gate, Gate + 0xfff, InnerGate, &seen));
    okay(outer->Run(Code, Code + sizeof outer_code - 1, 0));
    unsigned sum = 0, nearest = 0, ecx = 1, control = 0, inner_control = 0;
    okay(outer->Read(Data, &sum, 4));
    okay(outer->Read(Data + 4, &nearest, 4));
    okay(outer->Get(Reg::Ecx, ecx));
    okay(outer->Get(Reg::Fpcw, control));
    okay(inner->Get(Reg::Fpcw, inner_control));
    printf("outer_calls=%u inner_runs=%u inner_calls=%u sum=%u nearest=%u ecx=%u fpcw=%04x inner_fpcw=%04x\n",
           seen.outer_calls, seen.inner_runs, seen.inner_calls, sum, nearest, ecx, control, inner_control);
    // Every round: the inner machine answers 2 (down) and 3 (up), and the
    // outer guest rounds 2.5 to the nearest even, 2.
    assert(seen.inner_runs == Rounds && seen.inner_calls >= Rounds && ecx == 0);
    assert(sum == Rounds * 23 && nearest == Rounds * 2);
    assert(control == 0x027f && inner_control == down);
    printf("CPU NESTED RUN PASS engine=%s\n", outer->Backend() == X86::CpuBackend::Native ? "native" : outer->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete inner;
    delete outer;
    return 0;
}
