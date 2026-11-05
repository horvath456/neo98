// SPDX-License-Identifier: GPL-2.0-or-later
// Test code that changes instruction immediates during execution. Windows 98
// Cinepak decoding uses this behavior. After a write faults on a protected
// code page, FEX must stop the old translation and expose the new immediate
// to subsequent instructions.
#include "../src/x86_cpu.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static void okay(unsigned value) { assert(value == CpuError::Ok); }
// Code and data share the host page 10000h..13FFFh as ICCVID's heap does.
static constexpr unsigned Code = 0x10000, Data = 0x12000, Back = 0x30000, Stack = 0x40000;
static unsigned Call(X86::Cpu &cpu, unsigned function, unsigned eax)
{
    const unsigned esp = Stack + 0xff00;
    okay(cpu.Write(esp, &Back, 4));
    okay(cpu.Set(Reg::Esp, esp));
    okay(cpu.Set(Reg::Eax, eax));
    okay(cpu.Run(function, Back, 0));
    unsigned result = 0;
    okay(cpu.Get(Reg::Eax, result));
    return result;
}
int main()
{
    X86::Cpu *cpu = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), cpu));
    okay(cpu->Map(Code, 0x8000, Prot::All));
    okay(cpu->Map(Back, 0x4000, Prot::All));
    okay(cpu->Map(Stack, 0x10000, Prot::Read | Prot::Write));
    // First routine: store EAX into the immediate of the MOV six bytes on.
    // Code+0: A3 xx xx xx xx mov [Code+6], eax
    // Code+5: B8 11 11 11 11 mov eax, 11111111h
    // Code+10: C3 ret
    const unsigned char patch_mov[] = {0xa3, 0x06, 0x00, 0x01, 0x00, 0xb8, 0x11, 0x11, 0x11, 0x11, 0xc3};
    // Patch a loop limit, count up to the new value, and store the result.
    // This fixture exercises the self-modifying-code behavior needed by
    // Cinepak.
    const unsigned char patch_loop[] = {0xa3, 0x1e, 0x00, 0x01, 0x00, 0x33, 0xc9, 0x41, 0x81, 0xf9, 0xe8,
                                        0x03, 0x00, 0x00, 0x72, 0xf7, 0x89, 0x0d, 0x00, 0x20, 0x01, 0x00,
                                        0x8b, 0xc1, 0xc3};
    const unsigned char nop = 0x90;
    okay(cpu->Write(Code, patch_mov, sizeof patch_mov));
    okay(cpu->Write(Code + 20, patch_loop, sizeof patch_loop));
    okay(cpu->Write(Back, &nop, 1));
    okay(cpu->FlushCode());
    // Several rounds, so both the first store (a write fault on translated
    // code) and the later ones (the host page checks its own bytes) are seen.
    unsigned failures = 0;
    for (unsigned round = 0; round < 6; ++round)
    {
        const unsigned value = 0x100 + round, got = Call(*cpu, Code, value);
        const unsigned limit = 5 + round * 3, count = Call(*cpu, Code + 20, limit);
        printf("round=%u mov=%x expected=%x loop=%u expected=%u\n", round, got, value, count, limit);
        failures += (got != value) + (count != limit);
    }
    // Mutable MOV operands in every register, including ESP, must read the
    // current value without changing arithmetic flags. The guest writes the
    // operand itself; host invalidation would hide a stale-translation bug.
    for (unsigned reg = 0; reg < 8; ++reg)
    {
        const unsigned entry = Code + 0x100 + reg * 0x80;
        std::vector<unsigned char> bytes;
        auto byte = [&](unsigned n) { bytes.push_back((unsigned char)n); };
        auto word = [&](unsigned n) { for (unsigned i = 0; i < 4; ++i) byte(n >> (i * 8)); };
        if (reg == 4) { byte(0x89); byte(0x25); word(Data + 4); } // save ESP
        byte(0xa3); const auto patch = bytes.size(); word(0);
        byte(0x3d); word(5); // CMP EAX,5: MOV must preserve its flags
        byte(0xb8 + reg); const unsigned immediate = entry + bytes.size(); word(0xdeadbeef);
        byte(0x89); byte(0x05 | reg << 3); word(Data);
        if (reg == 4) { byte(0x8b); byte(0x25); word(Data + 4); } // restore ESP
        byte(0xc3);
        memcpy(bytes.data() + patch, &immediate, 4);
        okay(cpu->Write(entry, bytes.data(), bytes.size()));
        for (unsigned round = 0; round < 32; ++round)
        {
            const unsigned value = round & 1 ? 5 : 0;
            Call(*cpu, entry, value);
            unsigned stored = 0, flags = 0;
            okay(cpu->Read(Data, &stored, 4)); okay(cpu->Get(Reg::Eflags, flags));
            failures += stored != value || (flags & 0x8d5) != (value == 5 ? 0x44u : 0x91u);
        }
    }
    // The opcode is still code: patch MOV EAX to MOV ECX and back, through
    // guest stores on an already self-modifying page.
    const unsigned target = Code + 0x800, writer = Code + 0x900;
    const unsigned char target_bytes[] = {0xb8,0x78,0x56,0x34,0x12,0x8b,0xc1,0xc3};
    unsigned char writer_bytes[] = {0xa2,0,0,0,0,0xe9,0,0,0,0};
    memcpy(writer_bytes + 1, &target, 4);
    const unsigned rel = target - (writer + sizeof writer_bytes);
    memcpy(writer_bytes + 6, &rel, 4);
    okay(cpu->Write(target, target_bytes, sizeof target_bytes));
    okay(cpu->Write(writer, writer_bytes, sizeof writer_bytes));
    for (unsigned i = 0; i < 32; ++i)
    {
        okay(cpu->Set(Reg::Ecx, 0xabcdef));
        const unsigned value = Call(*cpu, writer, i & 1 ? 0xb8 : 0xb9);
        failures += value != (i & 1 ? 0xabcdefu : 0x12345678u);
    }
    // An immediate straddling both a guest page and the host's 16 KiB page.
    const unsigned edge = Code + 0x3ffd, edge_writer = Code + 0xa00;
    const unsigned char edge_bytes[] = {0xb8,0,0,0,0,0xc3};
    unsigned char edge_patch[] = {0xa3,0,0,0,0,0xe9,0,0,0,0};
    const unsigned edge_operand = edge + 1, edge_rel = edge - (edge_writer + sizeof edge_patch);
    memcpy(edge_patch + 1, &edge_operand, 4); memcpy(edge_patch + 6, &edge_rel, 4);
    okay(cpu->Write(edge, edge_bytes, sizeof edge_bytes));
    okay(cpu->Write(edge_writer, edge_patch, sizeof edge_patch));
    for (unsigned i = 0; i < 32; ++i) failures += Call(*cpu, edge_writer, 0x76543210 + i) != 0x76543210 + i;
    printf("mutable immediates: 256 register/flags, 32 opcode changes, 32 page-boundary cases; failures=%u\n", failures);
    assert(!failures);
    printf("CPU SELF PATCH PASS engine=%s\n", cpu->Backend() == X86::CpuBackend::Native ? "native" : cpu->Backend() == X86::CpuBackend::Fex ? "fex" : "unicorn");
    delete cpu;
    return 0;
}
