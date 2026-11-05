// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the native return provider with actual guest FLD/FSTP instructions.
// Build: clang++ -std=c++17 tools/test_x86_fpu_return.cpp src/x86_cpu_unicorn.cpp src/x86_address_space.cpp
// build/unicorn-host/lib/libunicorn.2.dylib -Wl,-rpath,build/unicorn-host/lib
#include "../src/x86_fpu_return.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using X86::Reg;
using X86::CpuError;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
struct State {
    uint16_t status{}, tags{}, control{};
    uint64_t registers[8][2]{};
};
static State snapshot(X86::Cpu *cpu) {
    State out;
    unsigned value = 0;
    okay(cpu->Get(Reg::Fpsw, value)); out.status = uint16_t(value);
    okay(cpu->Get(Reg::Fptag, value)); out.tags = uint16_t(value);
    okay(cpu->Get(Reg::Fpcw, value)); out.control = uint16_t(value);
    for (unsigned i = 0; i < 8; ++i)
        okay(cpu->GetWide(Reg::Fp0 + i, out.registers[i]));
    return out;
}
static bool same(const State &a, const State &b) {
    return a.status == b.status && a.tags == b.tags && a.control == b.control &&
           !memcmp(a.registers, b.registers, sizeof a.registers);
}
static void instructions(X86::Cpu *cpu, const unsigned char *code, unsigned length) {
    okay(cpu->Write(0x1000, code, length));
    okay(cpu->InvalidateCode(0x1000, 0x100));
    okay(cpu->Run(0x1000, 0x1000 + length, 100));
}
int main() {
    X86::Cpu *cpu = nullptr; okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(0x1000, 0x2000, X86::Prot::All));
    const unsigned char init[] = {0xdb, 0xe3};
    const unsigned char load[] = {0xdd, 0x05, 0x00, 0x20, 0, 0};
    const unsigned char store[] = {0xdd, 0x1d, 0x08, 0x20, 0, 0};
    const uint64_t values[] = {0, UINT64_C(0x8000000000000000), 1,
        UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000),
        UINT64_C(0x3ff0000000000000), UINT64_C(0xc008000000000000),
        UINT64_C(0x3fb999999999999a), UINT64_C(0x7fefffffffffffff),
        UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff8000000001234)};
    unsigned cases = 0;
    for (unsigned depth : {0u, 2u, 7u}) for (uint64_t value : values) {
        instructions(cpu, init, sizeof init);
        const uint64_t existing = UINT64_C(0x4004000000000000);
        okay(cpu->Write(0x2000, &existing, 8));
        for (unsigned i = 0; i < depth; ++i) instructions(cpu, load, sizeof load);
        X86::CpuContext *before = nullptr; okay(cpu->NewContext(before));
        okay(cpu->Save(before));
        okay(cpu->Write(0x2000, &value, 8));
        instructions(cpu, load, sizeof load); const State expected = snapshot(cpu);
        okay(cpu->Restore(before)); cpu->FreeContext(before);
        okay(X86FpuReturn::Push64(cpu, value)); assert(same(snapshot(cpu), expected));
        instructions(cpu, store, sizeof store);
        uint64_t result = 0; okay(cpu->Read(0x2008, &result, 8));
        assert(result == value); ++cases;
    }
    instructions(cpu, init, sizeof init);
    for (unsigned i = 0; i < 8; ++i) okay(X86FpuReturn::Push64(cpu, values[5]));
    const auto full = snapshot(cpu);
    assert(X86FpuReturn::Push64(cpu, 0) == CpuError::Exception && same(full, snapshot(cpu)));
    instructions(cpu, init, sizeof init); const auto empty = snapshot(cpu);
    assert(X86FpuReturn::Push64(cpu, UINT64_C(0x7ff0000000000001)) == CpuError::Argument);
    assert(same(empty, snapshot(cpu)));
    delete cpu;
    printf("X87 RETURN PASS actual guest FLD/FSTP cases=%u, full-stack and signalling-NaN boundaries\n", cases);
}
