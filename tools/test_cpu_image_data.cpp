// SPDX-License-Identifier: GPL-2.0-or-later
// PE data relaxation, revocation, sharing and saved memory.
#include "../src/x86_cpu.h"
#include "../src/pe32.h"
#include "../src/virtual_memory.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
using X86::Cpu;
using X86::CpuError;
using X86::Prot;
using X86::Reg;
static constexpr unsigned Base = 0x400000, Stack = 0x20000, Back = 0x30000;
static void okay(unsigned code) { assert(code == CpuError::Ok); }
static void word(std::vector<unsigned char> &v, unsigned at, unsigned value, unsigned bytes = 4)
{
    for (unsigned i = 0; i < bytes; ++i) v[at + i] = value >> (8 * i);
}
static std::vector<unsigned char> image(unsigned variant)
{
    std::vector<unsigned char> v(0x9000);
    word(v, 0, 0x5a4d, 2); word(v, 60, 0x80); word(v, 0x80, 0x4550);
    word(v, 0x84, 0x14c, 2); word(v, 0x86, 5, 2); word(v, 0x94, 224, 2);
    word(v, 0x96, 0x2102, 2); // DLL, relocatable
    const unsigned opt = 0x98;
    word(v, opt, 0x10b, 2); word(v, opt + 16, 0x1000);
    word(v, opt + 28, Base); word(v, opt + 32, 4096); word(v, opt + 36, 512);
    word(v, opt + 56, 0xc000); word(v, opt + 60, 0x1000);
    word(v, opt + 92, 16); word(v, opt + 136, 0x8000); word(v, opt + 140, 12);
    const char *names[] = {".text", ".rdata", ".data", ".bss", ".reloc"};
    unsigned rvas[] = {0x1000, 0x4000, 0x5000, 0x7000, 0x8000};
    unsigned sizes[] = {0x3000, 0x1000, 0x2000, 0x1000, 0x1000};
    unsigned flags[] = {0x60000020, 0x40000040, 0xc0000040, 0xc0000080, 0x40000040};
    if (variant == 1) flags[1] |= 0x20000000; // executable neighbour
    if (variant == 2) flags[1] |= 0x10000000; // shared neighbour
    if (variant == 3) flags[3] |= 0x10000000; // shared writable neighbour
    if (variant == 5) flags[2] &= ~0x80000000u, flags[3] &= ~0x80000000u;
    if (variant == 6)
    {
        sizes[1] = 0x3000; rvas[2] = 0x7000; sizes[2] = 0x1000; rvas[3] = 0x9000;
    }
    for (unsigned i = 0; i < 5; ++i)
    {
        const unsigned s = opt + 224 + i * 40;
        memcpy(v.data() + s, names[i], strlen(names[i]));
        word(v, s + 8, variant == 4 && i == 3 ? 0 : sizes[i]);
        word(v, s + 12, rvas[i]);
        word(v, s + 16, i == 3 ? 0 : sizes[i]); word(v, s + 20, i == 3 ? 0 : rvas[i]);
        word(v, s + 36, flags[i]);
    }
    v[0x1000] = 0x89; v[0x1001] = 0x06; v[0x1002] = 0xc3; // mov [esi],eax; ret
    word(v, 0x4000, Base + rvas[2]);
    word(v, 0x8000, 0x4000); word(v, 0x8004, 12); word(v, 0x8008, 0x3000, 2);
    return v;
}
struct Space final : Pe32::AddressSpace
{
    Cpu &cpu;
    VirtualMemory::Space memory;
    unsigned relaxed = 0;
    explicit Space(Cpu &c) : cpu(c) {}
    bool Map(unsigned a, unsigned n, unsigned p) override
    { return memory.Track(a, n, VirtualMemory::Space::Protection(p | Prot::Exec)) && !cpu.Map(a, n, p); }
    bool Store(unsigned a, const void *data, Pe32::Size n) override { return !cpu.Write(a, data, n); }
    bool Fetch(unsigned a, void *data, unsigned n) override { return !cpu.Read(a, data, n); }
    bool Protect(unsigned a, unsigned n, unsigned p) override
    { return !cpu.Protect(a, n, p) && memory.SetProtection(a, n, VirtualMemory::Space::Protection(p & 1 ? p | 4 : p)); }
    unsigned ImageDataPageSize() const override { return cpu.ImageDataPageSize(); }
    bool RelaxImageData(unsigned a, unsigned mask) override
    { ++relaxed; return !cpu.RelaxImageData(a, mask); }
};
struct Backend final : VirtualMemory::Backend
{
    Cpu &cpu;
    explicit Backend(Cpu &c) : cpu(c) {}
    bool Available(unsigned, unsigned) override { return true; }
    bool Map(unsigned a, unsigned n, unsigned p) override { return !cpu.Map(a, n, p & ~4u); }
    bool Unmap(unsigned a, unsigned n) override { return !cpu.Unmap(a, n); }
    bool Protect(unsigned a, unsigned n, unsigned p) override { return !cpu.Protect(a, n, p & ~4u); }
};
static unsigned run(Cpu &cpu, unsigned base, unsigned target)
{
    const unsigned esp = Stack + 0x3ff0;
    okay(cpu.Write(esp, &Back, 4));
    okay(cpu.Set(Reg::Esp, esp)); okay(cpu.Set(Reg::Esi, target)); okay(cpu.Set(Reg::Eax, 0x12345678));
    return cpu.Run(base + 0x1000, Back);
}
static void setup(Cpu &cpu)
{
    okay(cpu.Map(Stack, 0x4000, Prot::Read | Prot::Write));
    okay(cpu.Map(Back, 0x4000, Prot::Read | Prot::Exec));
    const unsigned char nop = 0x90;
    okay(cpu.Write(Back, &nop, 1));
}
static void watched(Cpu &, unsigned, unsigned, void *p) { ++*static_cast<unsigned *>(p); }
int main(int argc, char **argv)
{
    assert(argc == 3);
    const unsigned mode = strtoul(argv[1], nullptr, 0);
    Cpu *raw = nullptr;
    okay(X86::CreateCpu(X86::ProcessCpuBackend(), raw));
    if (atoi(argv[2])) raw = X86::CreateDualCpu(raw);
    std::unique_ptr<Cpu> owned(raw);
    auto &cpu = *owned;
    assert(cpu.ImageDataPageSize() == 16384);
    setup(cpu);
    auto bytes = image(mode >= 10 && mode <= 13 ? mode - 9 : mode == 15 ? 5 : mode == 16 ? 6 : 0);
    const auto original = bytes;
    Pe32::Image pe;
    assert(pe.Parse(bytes.data(), bytes.size()));
    auto space = std::make_unique<Space>(cpu);
    const unsigned base = mode == 14 ? 0x80000000 : mode == 17 ? 0x72000000 : mode == 2 ? 0x600000 : Base;
    assert(pe.Load(*space, base));
    const unsigned ro = base + 0x4000, rw = base + (mode == 16 ? 0x7000 : 0x5000);
    unsigned relocated = 0;
    okay(cpu.Read(ro, &relocated, 4)); assert(relocated == rw);
    unsigned zero = ~0u;
    okay(cpu.Read(base + (mode == 16 ? 0x9000 : 0x7000), &zero, 4)); assert(zero == 0);
    VirtualMemory::Info info{};
    assert(space->memory.Query(ro, info) && info.protection == 2);
    assert(space->memory.Query(rw, info) && info.protection == (mode == 15 ? 2u : 4u));
    if (mode >= 10 && mode <= 15)
    {
        assert(space->relaxed == 0);
        assert(run(cpu, base, ro) == CpuError::WriteProtected);
        if (mode == 15) assert(run(cpu, base, rw) == CpuError::WriteProtected);
        else okay(run(cpu, base, rw));
        if (mode == 13) assert(run(cpu, base, base + 0x7000) == CpuError::WriteProtected);
    }
    else
    {
        assert(space->relaxed == 1);
        for (unsigned i = 0; i < 128; ++i) okay(run(cpu, base, rw));
        if (mode == 0) okay(run(cpu, base, base + 0x7000));
        okay(run(cpu, base, ro + 4)); // documented exception, original R rights unchanged
        assert(space->memory.Query(ro, info) && info.protection == 2);
        assert(cpu.ImageDataReadOnly(ro) && !cpu.ImageDataReadOnly(rw));
        assert(run(cpu, base, base + 0x1008) == CpuError::WriteProtected);
        assert(run(cpu, base, base + 0xb000) == CpuError::WriteProtected);
        if (mode == 1)
        {
            Backend backend(cpu);
            assert(space->memory.Protect(backend, ro, 4096, 0x40000) == 0x40000);
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
            for (unsigned i = 0; i < 16; ++i) okay(run(cpu, base, rw)); // checked path again
            assert(space->memory.Protect(backend, rw, 4096, 0x40000) == 0x60000);
            assert(run(cpu, base, rw) == CpuError::WriteProtected);
            assert(space->memory.Protect(backend, rw, 4096, 0) == 0x40000);
            assert(run(cpu, base, rw) == CpuError::WriteProtected);
            assert(space->memory.Protect(backend, rw, 4096, 0x60000) == 0);
            okay(run(cpu, base, rw));
        }
        if (mode == 2)
        {
            // Unload after a guest write and reload the same relocated DLL.
            okay(cpu.Unmap(base, pe.SizeOfImage())); space->memory.Forget(base, pe.SizeOfImage());
            assert(pe.Load(*space, base));
            okay(cpu.Read(ro, &relocated, 4)); assert(relocated == rw);
            okay(run(cpu, base, ro + 4));
            okay(cpu.Unmap(base + 0x7000, 4096));
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
            okay(cpu.Map(base + 0x7000, 4096, Prot::Read | Prot::Write));
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
        }
        if (mode == 3)
        {
            unsigned writes = 0;
            okay(cpu.WatchWrites(watched, &writes));
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
            okay(run(cpu, base, rw)); assert(writes);
            cpu.RearmWrites(rw, 4096); okay(run(cpu, base, rw));
            cpu.UnwatchWrites();
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
        }
        if (mode == 4)
        {
            // Execute-on-demand must first revoke the writable data group.
            okay(cpu.Protect(ro, 4096, Prot::Read | Prot::Exec));
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
            okay(run(cpu, base, rw));
        }
        if (mode == 5 || mode == 16)
        {
            Cpu *copy = nullptr; okay(X86::CreateCpu(X86::ProcessCpuBackend(), copy));
            struct Copy { Cpu &from, &to; unsigned tagged = 0; } transfer{cpu, *copy};
            assert(cpu.SavedRegions([](void *context, unsigned a, unsigned n, unsigned p, void *host) {
                auto &c = *static_cast<Copy *>(context); assert(!host);
                if (p & Cpu::SavedImageReadOnly) ++c.tagged;
                okay(c.to.MapSaved(a, n, p));
                std::vector<unsigned char> data(n); okay(c.from.Read(a, data.data(), n));
                okay(c.to.Write(a, data.data(), n));
            }, &transfer));
            assert(transfer.tagged == (mode == 16 ? 3u : 1u));
            okay(run(*copy, base, ro + 4));
            okay(copy->Protect(rw, 4096, Prot::Read | Prot::Write));
            assert(run(*copy, base, ro + 4) == CpuError::WriteProtected);
            if (mode == 16)
            {
                assert(run(*copy, base, ro + 4096) == CpuError::WriteProtected);
                assert(run(*copy, base, ro + 8192) == CpuError::WriteProtected);
            }
            delete copy;
        }
        if (mode == 6)
        {
            // A new shared mapping in the group must not expose adjacent bytes.
            void *shared = nullptr; assert(!posix_memalign(&shared, 16384, 16384));
            memset(shared, 0x5a, 16384);
            okay(cpu.Unmap(base + 0x7000, 4096));
            okay(cpu.MapHost(base + 0x7000, 4096, Prot::Read | Prot::Write,
                             static_cast<unsigned char *>(shared) + 0x3000));
            okay(run(cpu, base, rw));
            assert(run(cpu, base, ro + 4) == CpuError::WriteProtected);
            assert(static_cast<unsigned char *>(shared)[0x1000] == 0x5a);
            okay(cpu.Unmap(base + 0x7000, 4096)); free(shared);
        }
    }
    assert(bytes == original); // the loader/relocation/guest never writes the file bytes
    printf("CPU IMAGE DATA PASS mode=%u dual=%s\n", mode, argv[2]);
}
