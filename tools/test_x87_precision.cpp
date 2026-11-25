// SPDX-License-Identifier: GPL-2.0-or-later
// x87 precision control under X86::Cpu, whichever engine NEO98_CPU selects.
// A Win32 thread starts at control word 027Fh (53-bit mantissa); FEXCore
// computes that with doubles and switches the process to 80-bit values when
// the guest loads a control word with PC=11b. Guest
// code divides 1 by 3 and stores the result as an 80-bit value, before and
// after FLDCW 037Fh, with a value on the stack across the switch. The control
// word of the CRT's math functions (133Fh) does not switch, and a process that
// has left 64-bit precision with an empty stack returns to doubles.
// Build: tools/test_x87_precision.py.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
struct Ten
{
    uint64_t mantissa;
    uint16_t top;
};
static Ten ReadTen(X86::Cpu *cpu, unsigned address)
{
    unsigned char bytes[10];
    okay(cpu->Read(address, bytes, sizeof bytes));
    Ten t;
    memcpy(&t.mantissa, bytes, 8);
    memcpy(&t.top, bytes + 8, 2);
    return t;
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(0x1000, 0x1000, Prot::All));
    okay(cpu->Map(0x2000, 0x1000, Prot::Read | Prot::Write));
    // Data: 1.0 and 3.0 as doubles at 2000h/2008h, control word 037Fh at
    // 2010h, results at 2100h, 2110h, 2120h.
    const double one = 1.0, three = 3.0;
    const uint16_t full = 0x037f;
    okay(cpu->Write(0x2000, &one, 8));
    okay(cpu->Write(0x2008, &three, 8));
    okay(cpu->Write(0x2010, &full, 2));
    const uint16_t crt_math = 0x133f, win32 = 0x027f;
    okay(cpu->Write(0x2012, &crt_math, 2));
    okay(cpu->Write(0x2014, &win32, 2));
    // 1/3 under the control word of the CRT's math functions, stored at 2140h.
    const unsigned char crt_code[] = {
        0xd9, 0x2d, 0x12, 0x20, 0x00, 0x00, // fldcw [2012h] (133Fh)
        0xdd, 0x05, 0x00, 0x20, 0x00, 0x00, // fld qword [2000h]
        0xdc, 0x35, 0x08, 0x20, 0x00, 0x00, // fdiv qword [2008h]
        0xdb, 0x3d, 0x40, 0x21, 0x00, 0x00, // fstp tword [2140h]
        0xd9, 0x2d, 0x14, 0x20, 0x00, 0x00, // fldcw [2014h] (027Fh)
        0xf4};
    const unsigned char leave_full[] = {
        0xd9, 0x2d, 0x14, 0x20, 0x00, 0x00, // fldcw [2014h] (027Fh)
        0xf4};
    const unsigned char idle[] = {0x90, 0xf4};
    okay(cpu->Write(0x1200, crt_code, sizeof crt_code));
    okay(cpu->Write(0x1300, leave_full, sizeof leave_full));
    okay(cpu->Write(0x1380, idle, sizeof idle));
    const unsigned char code[] = {
        0xd9, 0x3d, 0x30, 0x21, 0x00, 0x00, // fnstcw [2130h] (start value)
        0xdd, 0x05, 0x00, 0x20, 0x00, 0x00, // fld qword [2000h]
        0xdc, 0x35, 0x08, 0x20, 0x00, 0x00, // fdiv qword [2008h]
        0xd9, 0xc0,                         // fld st(0): a copy stays on the stack
        0xdb, 0x3d, 0x00, 0x21, 0x00, 0x00, // fstp tword [2100h] (53-bit)
        0xd9, 0x2d, 0x10, 0x20, 0x00, 0x00, // fldcw [2010h] (PC=64)
        0xdb, 0x3d, 0x10, 0x21, 0x00, 0x00, // fstp tword [2110h] (the copy)
        0xdd, 0x05, 0x00, 0x20, 0x00, 0x00, // fld qword [2000h]
        0xdc, 0x35, 0x08, 0x20, 0x00, 0x00, // fdiv qword [2008h]
        0xdb, 0x3d, 0x20, 0x21, 0x00, 0x00, // fstp tword [2120h] (64-bit)
        0xf4};
    okay(cpu->Write(0x1000, code, sizeof code));
    okay(cpu->InvalidateCode(0x1000, 0x1000));
    // A fresh Win32 thread (src/x86_fpu_return.h).
    okay(cpu->Set(Reg::Fpcw, 0x027f));
    okay(cpu->Set(Reg::Fpsw, 0));
    okay(cpu->Set(Reg::Fptag, 0xffff));
    const bool fex = cpu->Backend() == X86::CpuBackend::Fex;
    // FEXCore keeps doubles here, so the value has 53 bits; Unicorn has 64.
    const uint64_t crt_mantissa = fex ? 0xaaaaaaaaaaaaa800ull : 0xaaaaaaaaaaaaaaabull;
    okay(cpu->Run(0x1200, 0x1200 + sizeof crt_code - 1, 100));
    const Ten crt_before = ReadTen(cpu, 0x2140);
    assert(crt_before.top == 0x3ffd && crt_before.mantissa == crt_mantissa);
    okay(cpu->Run(0x1000, 0x1000 + sizeof code - 1, 100));
    const Ten rounded = ReadTen(cpu, 0x2100), kept = ReadTen(cpu, 0x2110), exact = ReadTen(cpu, 0x2120);
    // 1/3 = 0.0101... : exponent 3FFDh, mantissa AAAA...; 53 bits round up
    // to...AAAB with the low eleven bits clear, 64 bits to...AAAB.
    assert(rounded.top == 0x3ffd && rounded.mantissa == 0xaaaaaaaaaaaaa800ull);
    assert(kept.top == 0x3ffd && kept.mantissa == rounded.mantissa);
    assert(exact.top == 0x3ffd && exact.mantissa == 0xaaaaaaaaaaaaaaabull);
    uint16_t stored = 0;
    okay(cpu->Read(0x2130, &stored, 2));
    assert(stored == 0x027f);
    unsigned control = 0;
    okay(cpu->Get(Reg::Fpcw, control));
    assert(control == 0x037f);
    // Back at 53 bits with an empty stack: a second later the process has
    // doubles again, which the same computation shows on FEXCore (80-bit
    // values would give it 64 bits).
    okay(cpu->Run(0x1300, 0x1300 + sizeof leave_full - 1, 100));
    okay(cpu->Run(0x1380, 0x1380 + sizeof idle - 1, 100));
    usleep(1100000);
    okay(cpu->Run(0x1200, 0x1200 + sizeof crt_code - 1, 100));
    const Ten crt_after = ReadTen(cpu, 0x2140);
    assert(crt_after.top == 0x3ffd && crt_after.mantissa == crt_mantissa);
    printf("X87 PRECISION PASS engine=%s 53-bit %016llx 64-bit %016llx\n",
           cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn", (unsigned long long)rounded.mantissa,
           (unsigned long long)exact.mantissa);
    delete cpu;
    return 0;
}
