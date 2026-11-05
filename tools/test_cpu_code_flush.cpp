// SPDX-License-Identifier: GPL-2.0-or-later
// Translated code the guest is still in survives what a hook does.
// A gate hook runs while the block that called the gate is on the host
// stack. Under FEXCore, FlushInstructionCache (Cpu::FlushCode) inside the
// hook used to put the thread on a new code buffer and free the old one, and
// the block went on in freed memory (IE loading MSHTML: SEGV in
// ExitFunctionLink or at an unknown address). A nested Run that switches
// the x87 form (FLDCW 037Fh) clears the code cache the same way.
// Build: tools/test_cpu_code_flush.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
static constexpr unsigned Code = 0x1000, Data = 0x2000, Nested = 0x3000, Gate = 0x5000, Rounds = 40;
struct Seen
{
    unsigned calls = 0, nested = 0;
};
static void OnGate(X86::Cpu &cpu, unsigned, void *context)
{
    auto &seen = *static_cast<Seen *>(context);
    ++seen.calls;
    // FlushInstructionCache on every call.
    okay(cpu.FlushCode());
    // Once: a callback into the guest that asks for 64-bit precision.
    if (seen.calls == Rounds / 2)
    {
        unsigned eip = 0, esp = 0;
        okay(cpu.Get(Reg::Eip, eip));
        okay(cpu.Get(Reg::Esp, esp));
        okay(cpu.Run(Nested, Nested + 6, 0));
        ++seen.nested;
        okay(cpu.Set(Reg::Esp, esp));
        okay(cpu.Set(Reg::Eip, eip));
    }
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(Code, 0x1000, Prot::All));
    okay(cpu->Map(Data, 0x1000, Prot::Read | Prot::Write));
    okay(cpu->Map(Nested, 0x1000, Prot::All));
    okay(cpu->Map(Gate, 0x1000, Prot::All));
    okay(cpu->Map(0x10000, 0x10000, Prot::Read | Prot::Write));
    // The loop calls a function at 4000h that calls the gate and returns
    // with a translated RET: its return address comes from the call-return
    // stack of FEXCore, a host address of the code before the flush.
    const unsigned char loop[] = {
        0xb9, Rounds, 0x00, 0x00, 0x00,         // mov ecx, Rounds
        0xe8, 0xf6, 0x2f, 0x00, 0x00,           // l: call 4000h
        0xff, 0x05, 0x00, 0x20, 0x00, 0x00,     // inc dword [2000h]
        0x49,                                   // dec ecx
        0x75, 0xf2,                             // jnz l
        0x90};                                  // nop: the end of the Run
    // The call at 1005h: 1005h + 5 + 2FF6h = 4000h; jnz at 1011h back to 1005h.
    const unsigned char function[] = {
        0xe8, 0xfb, 0x0f, 0x00, 0x00,           // call 5000h (the gate)
        0xff, 0x05, 0x08, 0x20, 0x00, 0x00,     // inc dword [2008h]
        0xc3};                                  // ret
    okay(cpu->Map(0x4000, 0x1000, Prot::All));
    okay(cpu->Write(0x4000, function, sizeof function));
    okay(cpu->InvalidateCode(0x4000, 0x1000));
    okay(cpu->Write(Code, loop, sizeof loop));
    const unsigned char nested[] = {
        0xd9, 0x2d, 0x10, 0x20, 0x00, 0x00,     // fldcw [2010h] (037Fh)
        0x90};                                  // until
    okay(cpu->Write(Nested, nested, sizeof nested));
    const unsigned char ret = 0xc3;
    okay(cpu->Write(Gate, &ret, 1));
    const uint16_t full = 0x037f;
    okay(cpu->Write(Data + 0x10, &full, 2));
    okay(cpu->InvalidateCode(Code, 0x1000));
    okay(cpu->InvalidateCode(Nested, 0x1000));
    okay(cpu->InvalidateCode(Gate, 0x1000));
    okay(cpu->Set(Reg::Fpcw, 0x027f));
    okay(cpu->Set(Reg::Esp, 0x1fff0));
    Seen seen;
    okay(cpu->SetGateHook(Gate, Gate + 1, OnGate, &seen));
    okay(cpu->Run(Code, Code + sizeof loop - 1, 0));
    unsigned count = 0, ecx = 1, control = 0;
    okay(cpu->Read(Data, &count, 4));
    unsigned inner = 0;
    okay(cpu->Read(Data + 8, &inner, 4));
    okay(cpu->Get(Reg::Ecx, ecx));
    okay(cpu->Get(Reg::Fpcw, control));
    printf("calls=%u count=%u ecx=%u fpcw=%04x nested=%u\n", seen.calls, count, ecx, control, seen.nested);
    // Unicorn may run the hook of the gate once more after a flush inside it
    // (the instruction is translated anew); the guest ran every round once.
    assert(seen.calls >= Rounds && count == Rounds && inner == Rounds && ecx == 0);
    assert(control == 0x037f && seen.nested == 1);
    printf("CPU CODE FLUSH PASS engine=%s\n", cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete cpu;
    return 0;
}
