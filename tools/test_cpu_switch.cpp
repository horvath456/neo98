// SPDX-License-Identifier: GPL-2.0-or-later
// Guests that take turns on the native processor of Linux x64. In the direct
// mode they share the 32-bit addresses and the LDT of the host process, and
// a change from one to the next must cost what differs between the two, not
// what each of them has: a Windows process that plays a video through a
// 16-bit driver holds several hundred segments and a hundred runs of page
// protections, and takes its turns with flat processes that hold neither.
//
// Four kinds of guest:
//   driver   400 descriptors in its LDT, 150 private pages whose protection
//            changes from page to page, and a block of shared memory
//   second   the driver's first 360 descriptors, 40 with another base, and
//            one of its own: a second process with the same 16-bit modules
//   flat     an empty LDT and other private pages at the same addresses
//   bare     no LDT at all
//   two with a GDT of their own, as a Windows process has: flat code and
//            data, a segment for the thread block at FS with a base of its
//            own, and in one of them a further segment at GS
// Each one runs a routine of flat 32-bit code on the processor itself (no
// single steps, so nothing between the guest and the host's tables): it
// reads its pages, asks the processor with LSL which selectors there are,
// and reads through some of them. After many turns every guest still reads
// its own pages and no others, a guest with descriptors finds exactly its
// own table (not a selector it freed, not one of the guest before), and
// rights, mappings and descriptors changed while another guest stood at the
// addresses are in force at the next turn. A data segment register that still
// holds a selector the guest has freed starts the next run as the null selector.
//
// The descriptors of a GDT stand translated in the upper half of the host's
// LDT; every guest finds there its own and none of another.
//
// A guest without descriptors is not asked what the lower half of the LDT
// holds: it leaves that half as it finds it (DirectCpu::SyncLdt), as a flat
// process of Windows finds the selectors of the 16-bit side in the one LDT
// of the system. NEO98_DIRECT_EXACT_LDT=1 empties it for such a guest too;
// the test then asks the flat guests as well and drops the bound that this
// costs.
//
// The direct mode counts the system calls of its changes (Cpu::SwitchCosts);
// the bounds are on those counts, never on time. The ptrace mode gives each
// guest a process of its own and has nothing to count.
#include "../src/x86_cpu.h"
#include "../src/linux_shared_memory.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <memory>
#include <vector>
using namespace X86;
namespace
{
constexpr unsigned Page = 4096;
constexpr unsigned Code = 0x10000, CodePages = 16, Stack = 0x30000, Result = 0x40000, ResultPages = 4;
constexpr unsigned Gates = 0x50000, Ldt = 0x60000, Gdt = 0x70000, Segments = 0x100000, Pages = 0x400000;
constexpr unsigned Shared = 0x80000000u, SharedPages = 64;
constexpr unsigned Invalid = 0xffffffffu;
void okay(unsigned error)
{
    if (!error)
        return;
    fprintf(stderr, "CPU error %u: %s\n", error, CpuErrorText(error));
    void *frames[8];
    backtrace_symbols_fd(frames, backtrace(frames, 8), 2);
    abort();
}
#define CHECK(condition, ...)                                                                                         \
    do                                                                                                                \
    {                                                                                                                 \
        if (!(condition))                                                                                             \
        {                                                                                                             \
            fprintf(stderr, "CPU SWITCH FAIL %s:%d: ", __FILE__, __LINE__);                                           \
            fprintf(stderr, __VA_ARGS__);                                                                             \
            fprintf(stderr, "\n");                                                                                    \
            exit(1);                                                                                                  \
        }                                                                                                             \
    } while (0)

// A data segment of 16-bit code: present, ring 3, writable, with a limit in
// bytes that tells the descriptors apart.
unsigned long long Descriptor(unsigned base, unsigned limit)
{
    return (limit & 0xffff) | (static_cast<unsigned long long>(base & 0xffffff) << 16) | (0xf2ull << 40) |
           (static_cast<unsigned long long>((limit >> 16) & 0xf) << 48) |
           (static_cast<unsigned long long>(base >> 24) << 56);
}
void Stop(Cpu &cpu, unsigned, void *) { cpu.Stop(); }

struct Guest
{
    const char *name;
    unsigned id;
    std::unique_ptr<Cpu> cpu;
    // What the guest was given.
    std::vector<unsigned long long> ldt = std::vector<unsigned long long>(512);
    bool has_ldt = false;
    std::vector<unsigned long long> gdt = std::vector<unsigned long long>(16);
    bool has_gdt = false;
    unsigned pages = 0;         // private pages at Pages, protection by page
    unsigned private_runs = 0;  // mmap calls its private memory needs
    std::vector<unsigned char> shared = std::vector<unsigned char>(SharedPages); // rights by page, 0 = not mapped

    unsigned Marker(unsigned page) const { return id * 1000000 + page; }
    static unsigned Protection(unsigned page)
    {
        const unsigned cycle[] = {Prot::Read, Prot::Read | Prot::Write, Prot::Read | Prot::Exec};
        return cycle[page % 3];
    }
    void Make(const char *called, unsigned number, unsigned private_pages)
    {
        name = called;
        id = number;
        pages = private_pages;
        Cpu *made = nullptr;
        okay(CreateCpu(CpuBackend::Native, made));
        cpu.reset(made);
        okay(cpu->Map(Code, CodePages * Page, Prot::All));
        okay(cpu->Map(Stack, Page, Prot::Read | Prot::Write));
        okay(cpu->Map(Result, ResultPages * Page, Prot::Read | Prot::Write));
        okay(cpu->Map(Gates, Page, Prot::All));
        okay(cpu->SetGateHook(Gates, Gates + Page - 1, Stop, nullptr));
        // The bytes the segments of the driver point at.
        okay(cpu->Map(Segments, 512 * Page, Prot::Read | Prot::Write));
        for (unsigned slot = 0; slot < 512; ++slot)
        {
            const unsigned value = id * 1000000 + 500000 + slot;
            okay(cpu->Write(Segments + slot * Page, &value, 4));
        }
        private_runs = 5;
        // Allocated out of order, as a heap grows: the host may join what is
        // adjacent, but the rights of every page are its own.
        for (unsigned odd = 0; odd < 2; ++odd)
            for (unsigned page = odd; page < pages; page += 2)
            {
                okay(cpu->Map(Pages + page * Page, Page, Prot::Read | Prot::Write));
                const unsigned value = Marker(page);
                okay(cpu->Write(Pages + page * Page, &value, 4));
            }
        for (unsigned page = 0; page < pages; ++page)
            okay(cpu->Protect(Pages + page * Page, Page, Protection(page)));
        private_runs += pages;
        okay(cpu->Set(Reg::Esp, Stack + 4000));
    }
    void GiveLdt()
    {
        has_ldt = true;
        okay(cpu->Map(Ldt, Page, Prot::Read | Prot::Write));
        ++private_runs;
        okay(cpu->Write(Ldt, ldt.data(), Page));
        DescriptorTable table;
        table.base = Ldt;
        table.limit = Page - 1;
        okay(cpu->SetWide(Reg::Ldtr, &table));
    }
    // The table of a Windows process: null, flat code, flat data, the thread
    // block, and where asked a segment more. FS and GS name the last two.
    void GiveGdt(bool more)
    {
        has_gdt = true;
        gdt[1] = 0x00cf9a000000ffffull;
        gdt[2] = 0x00cf92000000ffffull;
        gdt[3] = Descriptor(Segments + 410 * Page, 0x2000 + id);
        if (more)
            gdt[5] = Descriptor(Segments + 420 * Page, 0x2222);
        okay(cpu->Map(Gdt, Page, Prot::Read | Prot::Write));
        ++private_runs;
        okay(cpu->Write(Gdt, gdt.data(), 16 * 8));
        DescriptorTable table;
        table.base = Gdt;
        table.limit = (more ? 6 : 4) * 8 - 1;
        okay(cpu->SetWide(Reg::Gdtr, &table));
        okay(cpu->Set(Reg::Cr0, 1));
        okay(cpu->Set(Reg::Cs, 8));
        for (int reg : {Reg::Ds, Reg::Es, Reg::Ss})
            okay(cpu->Set(reg, 0x10));
        okay(cpu->Set(Reg::Fs, 0x18));
        okay(cpu->Set(Reg::Gs, more ? 0x28 : 0));
    }
    void SetDescriptor(unsigned slot, unsigned long long value)
    {
        ldt[slot] = value;
        okay(cpu->Write(Ldt + slot * 8, &value, 8));
    }
    void MapShared(unsigned char *block, unsigned page, unsigned rights)
    {
        okay(cpu->MapHost(Shared + page * Page, Page, rights, block + page * Page));
        shared[page] = static_cast<unsigned char>(rights);
    }
    bool Own() const
    {
        for (auto value : ldt)
            if (value)
                return true;
        return false;
    }

    // The routine: reads, LSL of selectors, reads through selectors, and
    // the jump into the gate that ends the run.
    std::vector<unsigned> Run(const std::vector<unsigned> &reads, const std::vector<unsigned> &selectors,
                              const std::vector<unsigned> &through, bool thread_block = false)
    {
        std::vector<unsigned char> code;
        auto byte = [&](std::initializer_list<unsigned> list) {
            for (unsigned value : list)
                code.push_back(static_cast<unsigned char>(value));
        };
        auto word = [&](unsigned value) {
            for (unsigned n = 0; n < 4; ++n)
                code.push_back(static_cast<unsigned char>(value >> (8 * n)));
        };
        unsigned out = Result;
        auto store = [&] {
            byte({0xa3});
            word(out);
            out += 4;
        };
        for (unsigned address : reads)
        {
            byte({0xa1}); // mov eax,[address]
            word(address);
            store();
        }
        for (unsigned selector : selectors)
        {
            byte({0xb9}); // mov ecx,selector
            word(selector);
            byte({0x31, 0xc0, 0x0f, 0x03, 0xc1, 0x74, 0x05, 0xb8}); // xor eax,eax; lsl eax,ecx; jz +5; mov eax,-1
            word(Invalid);
            store();
        }
        for (unsigned selector : through)
        {
            byte({0xb9});
            word(selector);
            byte({0x8e, 0xc1, 0x26, 0xa1, 0, 0, 0, 0}); // mov es,ecx; mov eax,es:[0]
            store();
        }
        if (thread_block)
        {
            byte({0x64, 0xa1, 0, 0, 0, 0}); // mov eax,fs:[0]
            store();
        }
        byte({0x1e, 0x07, 0xe9}); // push ds; pop es; jmp Gates
        word(Gates - (Code + static_cast<unsigned>(code.size()) + 4));
        CHECK(code.size() <= CodePages * Page && out - Result <= ResultPages * Page, "the routine of %s is too long", name);
        okay(cpu->Write(Code, code.data(), static_cast<unsigned>(code.size())));
        const unsigned error = cpu->Run(Code, 0);
        CHECK(!error, "%s: the routine stopped with %s", name, CpuErrorText(error));
        std::vector<unsigned> result((out - Result) / 4);
        okay(cpu->Read(Result, result.data(), out - Result));
        return result;
    }
    // One instruction with single steps: A1 reads, A3 writes the address.
    unsigned Access(unsigned opcode, unsigned address)
    {
        unsigned char code[] = {static_cast<unsigned char>(opcode), 0, 0, 0, 0};
        memcpy(code + 1, &address, 4);
        okay(cpu->Write(Code, code, sizeof code));
        return cpu->Run(Code, Code + sizeof code, 20);
    }

    // Everything the guest can see of itself and of the others.
    void Look(const std::vector<Guest *> &all, bool exact_flat)
    {
        std::vector<unsigned> reads, selectors, through;
        for (unsigned page = 0; page < pages; ++page)
            reads.push_back(Pages + page * Page);
        for (unsigned page = 0; page < SharedPages; ++page)
            if (shared[page])
                reads.push_back(Shared + page * Page);
        const bool asked = has_ldt ? (Own() || exact_flat) : exact_flat;
        if (asked)
            for (unsigned slot = 1; slot < 512; ++slot)
                selectors.push_back(slot * 8 + 7);
        if (has_ldt)
            for (unsigned slot = 1; slot < 512; slot += 7)
                if (ldt[slot])
                    through.push_back(slot * 8 + 7);
        // The translated descriptors of a GDT, above the flat two.
        for (unsigned index = 3; index < 16; ++index)
            selectors.push_back((4096 + index) * 8 + 7);
        const auto result = Run(reads, selectors, through, has_gdt);
        unsigned at = 0;
        for (unsigned page = 0; page < pages; ++page, ++at)
            CHECK(result[at] == Marker(page), "%s reads %u at its page %u, not its own %u", name, result[at], page,
                  Marker(page));
        for (unsigned page = 0; page < SharedPages; ++page)
            if (shared[page])
            {
                CHECK(result[at] == 7000000 + page, "%s reads %u in shared page %u", name, result[at], page);
                ++at;
            }
        if (asked)
            for (unsigned slot = 1; slot < 512; ++slot, ++at)
            {
                const unsigned expected = has_ldt && ldt[slot] ? static_cast<unsigned>(ldt[slot] & 0xffff) : Invalid;
                CHECK(result[at] == expected, "%s: selector %04x has the limit %x, expected %x", name, slot * 8 + 7,
                      result[at], expected);
            }
        for (unsigned index = 3; index < 16; ++index, ++at)
        {
            const unsigned expected = has_gdt && gdt[index] ? static_cast<unsigned>(gdt[index] & 0xffff) : Invalid;
            CHECK(result[at] == expected, "%s: the descriptor %u of a GDT has the limit %x, expected %x", name, index,
                  result[at], expected);
        }
        if (has_ldt)
            for (unsigned slot = 1; slot < 512; slot += 7)
                if (ldt[slot])
                {
                    const unsigned base = static_cast<unsigned>((ldt[slot] >> 16) & 0xffffff) |
                                          static_cast<unsigned>((ldt[slot] >> 56) << 24);
                    unsigned expected = 0;
                    okay(cpu->Read(base, &expected, 4));
                    CHECK(result[at] == expected && expected / 1000000 == id,
                          "%s reads %u through selector %04x, its segment holds %u", name, result[at], slot * 8 + 7,
                          expected);
                    ++at;
                }
        if (has_gdt)
            CHECK(result[at] == id * 1000000 + 500410, "%s reads %u at FS:0, not its own thread block", name, result[at]);
        // What another guest has and this one does not is no memory here.
        for (const Guest *other : all)
        {
            if (other == this)
                continue;
            for (unsigned page : {pages, other->pages - 1})
                if (page >= pages && page < other->pages)
                    CHECK(Access(0xa1, Pages + page * Page) == CpuError::ReadUnmapped,
                          "%s reads page %u, which only %s has", name, page, other->name);
            for (unsigned page = 0; page < SharedPages; page += 5)
                if (!shared[page] && other->shared[page])
                    CHECK(Access(0xa1, Shared + page * Page) == CpuError::ReadUnmapped,
                          "%s reads shared page %u, which only %s has", name, page, other->name);
        }
        // Rights: the cycle of the private pages, and the shared pages.
        for (unsigned page = 0; page < pages && page < 6; ++page)
        {
            const unsigned error = Access(0xa3, Pages + page * Page);
            if (Protection(page) & Prot::Write)
            {
                okay(error);
                const unsigned value = Marker(page);
                okay(cpu->Write(Pages + page * Page, &value, 4));
            }
            else
                CHECK(error == CpuError::WriteProtected, "%s writes its read-only page %u", name, page);
        }
        for (unsigned page = 0; page < SharedPages; page += 9)
            if (shared[page] && !(shared[page] & Prot::Write))
                CHECK(Access(0xa3, Shared + page * Page) == CpuError::WriteProtected,
                      "%s writes shared page %u, which it may only read", name, page);
    }
};

struct Count
{
    SwitchCost before = {};
    bool counted = false;
    explicit Count(Cpu &cpu) { counted = cpu.SwitchCosts(before); }
    SwitchCost Since(Cpu &cpu) const
    {
        SwitchCost now = {};
        cpu.SwitchCosts(now);
        return {now.switches - before.switches, now.map_calls - before.map_calls,
                now.descriptor_calls - before.descriptor_calls, now.microseconds - before.microseconds};
    }
};
// Two guests take turns, each with a short routine that touches a page.
SwitchCost Alternate(Guest &first, Guest &second, unsigned rounds)
{
    const std::vector<unsigned> none;
    first.Run({Pages}, none, none);
    second.Run({Pages}, none, none);
    Count count(*first.cpu);
    for (unsigned round = 0; round < rounds; ++round)
    {
        first.Run({Pages}, none, none);
        second.Run({Pages}, none, none);
    }
    return count.Since(*first.cpu);
}
void Report(const char *what, const SwitchCost &cost)
{
    if (cost.switches)
        printf("  %-44s %6.1f mmap, %6.1f modify_ldt, %7.1f microseconds per change (%llu changes)\n", what,
               double(cost.map_calls) / cost.switches, double(cost.descriptor_calls) / cost.switches,
               double(cost.microseconds) / cost.switches, cost.switches);
}
} // namespace

int main()
{
    const bool exact_flat = getenv("NEO98_DIRECT_EXACT_LDT") != nullptr;
    auto *block = static_cast<unsigned char *>(LinuxSharedMemory::Allocate(SharedPages * Page));
    CHECK(block, "no shared memory");
    for (unsigned page = 0; page < SharedPages; ++page)
    {
        const unsigned value = 7000000 + page;
        memcpy(block + page * Page, &value, 4);
    }
    Guest driver, second, flat, bare, windows, more;
    driver.Make("the driver", 1, 150);
    second.Make("the second", 2, 40);
    flat.Make("the flat guest", 3, 12);
    bare.Make("the bare guest", 4, 6);
    windows.Make("the guest with a GDT", 6, 10);
    more.Make("the guest with a larger GDT", 7, 10);
    windows.GiveGdt(false);
    more.GiveGdt(true);
    driver.GiveLdt();
    second.GiveLdt();
    flat.GiveLdt();
    for (unsigned slot = 4; slot < 404; ++slot)
        driver.SetDescriptor(slot, Descriptor(Segments + slot * Page, 0x1000 + slot));
    for (unsigned slot = 4; slot < 404; ++slot)
        second.SetDescriptor(slot, slot < 364 ? driver.ldt[slot] : Descriptor(Segments + (slot - 300) * Page, 0x3000 + slot));
    second.SetDescriptor(500, Descriptor(Segments + 500 * Page, 0x3500));
    // Shared memory: every guest has the even pages, one by one (the gaps
    // keep them apart), the flat guest a few of them read-only, and the
    // driver the odd pages as well.
    unsigned common = 0;
    for (unsigned page = 0; page < SharedPages; page += 2)
    {
        driver.MapShared(block, page, Prot::Read | Prot::Write);
        second.MapShared(block, page, Prot::Read | Prot::Write);
        flat.MapShared(block, page, page % 18 ? Prot::Read | Prot::Write : Prot::Read);
        bare.MapShared(block, page, Prot::Read | Prot::Write);
        windows.MapShared(block, page, Prot::Read | Prot::Write);
        more.MapShared(block, page, Prot::Read | Prot::Write);
        common += page % 18 != 0;
    }
    for (unsigned page = 1; page < SharedPages; page += 2)
        driver.MapShared(block, page, Prot::Read | Prot::Write);
    const std::vector<Guest *> all = {&driver, &second, &flat, &bare, &windows, &more};

    SwitchCost probe = {};
    const bool counted = driver.cpu->SwitchCosts(probe);
    for (unsigned round = 0; round < 3; ++round)
        for (Guest *guest : all)
            guest->Look(all, exact_flat);

    // The bounds. A guest with several hundred segments and a flat one: no
    // descriptor is written, and what both map alike is not mapped again.
    constexpr unsigned rounds = 200;
    const SwitchCost with_flat = Alternate(driver, flat, rounds);
    const SwitchCost with_bare = Alternate(driver, bare, rounds);
    const SwitchCost with_second = Alternate(driver, second, rounds);
    const SwitchCost flat_pair = Alternate(flat, bare, rounds);
    const SwitchCost gdt_pair = Alternate(windows, more, rounds);
    const SwitchCost driver_windows = Alternate(driver, windows, rounds);
    if (counted)
    {
        Report("400 segments, 150 runs / flat guest", with_flat);
        Report("400 segments, 150 runs / no LDT", with_bare);
        Report("400 segments / 360 of them and 41 others", with_second);
        Report("two flat guests", flat_pair);
        Report("two guests with a GDT", gdt_pair);
        Report("400 segments, 150 runs / guest with a GDT", driver_windows);
        CHECK(with_flat.switches == 2 * rounds && with_bare.switches == 2 * rounds &&
                  with_second.switches == 2 * rounds && flat_pair.switches == 2 * rounds,
              "%llu, %llu, %llu and %llu changes in %u rounds", with_flat.switches, with_bare.switches,
              with_second.switches, flat_pair.switches, rounds);
        // In a round of two changes: each private run of both guests once;
        // the 32 odd shared pages of the driver twice (mapped for it, taken
        // away for the other, each between two pages that stay); the shared
        // pages whose rights differ twice; and a few calls for the rest of
        // what the guest before had. Never the shared pages both have alike,
        // and never one call for each page the other guest had.
        auto maps = [&](const Guest &a, const Guest &b, unsigned differing) {
            return rounds * (a.private_runs + b.private_runs + 2 * 32 + 2 * differing + 8ull);
        };
        CHECK(with_flat.map_calls <= maps(driver, flat, SharedPages / 2 - common),
              "driver and flat guest: %llu mmap calls in %u rounds, at most %llu", with_flat.map_calls, rounds,
              maps(driver, flat, SharedPages / 2 - common));
        CHECK(with_bare.map_calls <= maps(driver, bare, 0), "driver and bare guest: %llu mmap calls, at most %llu",
              with_bare.map_calls, maps(driver, bare, 0));
        if (!exact_flat)
        {
            CHECK(with_bare.descriptor_calls == 0, "driver and bare guest: %llu descriptor writes",
                  with_bare.descriptor_calls);
            CHECK(with_flat.descriptor_calls == 0, "driver and flat guest: %llu descriptor writes",
                  with_flat.descriptor_calls);
        }
        // 40 descriptors differ and 41 are in one table only.
        CHECK(with_second.descriptor_calls <= 2 * rounds * 81ull,
              "driver and second: %llu descriptor writes in %u rounds, at most %llu", with_second.descriptor_calls,
              rounds, 2 * rounds * 81ull);
        CHECK(flat_pair.descriptor_calls == 0, "two flat guests: %llu descriptor writes", flat_pair.descriptor_calls);
        // The thread block has another base in each and one of the two has a
        // segment more: two writes a change. Flat code and data stay.
        CHECK(gdt_pair.descriptor_calls <= 2 * rounds * 2ull, "two guests with a GDT: %llu descriptor writes",
              gdt_pair.descriptor_calls);
        // The driver has no GDT: the three descriptors of the other leave and come.
        if (!exact_flat)
            CHECK(driver_windows.descriptor_calls <= 2 * rounds * 3ull,
                  "driver and guest with a GDT: %llu descriptor writes", driver_windows.descriptor_calls);
    }
    for (Guest *guest : all)
        guest->Look(all, exact_flat);

    // Changes made while another guest stands at the addresses.
    flat.Run({Pages}, {}, {});
    driver.SetDescriptor(10, 0);                                                // a segment freed
    driver.SetDescriptor(11, Descriptor(Segments + 300 * Page, 0x1f00));        // one moved
    driver.SetDescriptor(450, Descriptor(Segments + 450 * Page, 0x1450));       // one allocated
    okay(driver.cpu->Unmap(Shared + 3 * Page, Page));
    driver.shared[3] = 0;
    okay(driver.cpu->Protect(Shared + 4 * Page, Page, Prot::Read));
    driver.shared[4] = Prot::Read;
    okay(driver.cpu->Unmap(Pages + 149 * Page, Page));
    driver.pages = 149;
    for (Guest *guest : all)
        guest->Look(all, exact_flat);
    CHECK(driver.Access(0xa1, Shared + 3 * Page) == CpuError::ReadUnmapped, "the driver reads a page it unmapped");
    // The same while the guest itself stands there, seen after a turn of another.
    driver.Run({Pages}, {}, {});
    driver.SetDescriptor(12, 0);
    okay(driver.cpu->Protect(Shared + 6 * Page, Page, Prot::Read));
    driver.shared[6] = Prot::Read;
    second.SetDescriptor(20, 0);
    for (unsigned turn = 0; turn < 2; ++turn)
        for (Guest *guest : all)
            guest->Look(all, exact_flat);

    // A selector the guest freed while ES still held it: 16-bit code frees a
    // block and leaves the selector in the register. The run starts all the
    // same, with the null selector there, as Windows loads it when it finds
    // no descriptor for a register it restores.
    {
        const unsigned slot = 460, selector = slot * 8 + 7;
        driver.SetDescriptor(slot, Descriptor(Segments + slot * Page, 0x1460));
        okay(driver.cpu->Set(Reg::Es, selector));
        driver.SetDescriptor(slot, 0);
        // mov eax,11111111h; push es; pop eax; mov [Result],eax; jmp Gates
        unsigned char code[] = {0xb8, 0x11, 0x11, 0x11, 0x11, 0x06, 0x58, 0xa3, 0, 0, 0, 0, 0xe9, 0, 0, 0, 0};
        const unsigned result = Result, jump = Gates - (Code + sizeof code);
        memcpy(code + 8, &result, 4);
        memcpy(code + 13, &jump, 4);
        okay(driver.cpu->Write(Code, code, sizeof code));
        const unsigned error = driver.cpu->Run(Code, 0);
        CHECK(!error, "a run with a freed selector in ES stopped with %s", CpuErrorText(error));
        unsigned seen = ~0u;
        okay(driver.cpu->Read(Result, &seen, 4));
        CHECK((seen & 0xffff) == 0, "ES held %04x at the start of a run after its selector was freed", seen & 0xffff);
        for (Guest *guest : all)
            guest->Look(all, exact_flat);
    }

    // A guest that ends takes its descriptors with it; the others keep theirs.
    {
        Guest passing;
        passing.Make("the passing guest", 5, 8);
        passing.GiveLdt();
        for (unsigned slot = 300; slot < 480; ++slot)
            passing.SetDescriptor(slot, Descriptor(Segments + slot * Page, 0x5000 + slot));
        const std::vector<Guest *> with = {&driver, &second, &flat, &bare, &windows, &more, &passing};
        for (Guest *guest : with)
            guest->Look(with, exact_flat);
    }
    for (unsigned turn = 0; turn < 2; ++turn)
        for (Guest *guest : all)
            guest->Look(all, exact_flat);
    puts(counted ? "CPU SWITCH PASS: memory, descriptors and the system calls of a change"
                 : "CPU SWITCH PASS: memory and descriptors (a process for each guest, nothing to count)");
    return 0;
}
