// SPDX-License-Identifier: GPL-2.0-or-later
// The machine of ARM64 processes on macOS: Hypervisor.framework.
// macOS keeps the low 4 GiB of a 64-bit process unmapped, so a program whose
// pointers are the 32-bit guest addresses of this port cannot run inside the
// host process. It runs on EL0 of a guest instead:
// - one VM per host process, one virtual processor per host thread that
// runs guest code;
// - per Win98 process 4 GiB of host address space, committed on touch and
// mapped as a whole into the intermediate address space at slot * 4 GiB;
// - a stage 1 table per process with 4 KiB pages (TTBR0, T0SZ 32) that
// carries the page rights of the process; host bytes several processes
// share (the session arena) are mapped at a place of their own and the
// pages point there;
// - one page of EL1 code in the upper half (TTBR1): the exception vector,
// whose every entry is `hvc #slot; eret`, and the entry stubs.
// The machine is taken down in order before the host process ends or replaces
// itself (Shutdown below): every thread destroys its own processor, then the
// VM goes. A process that left with a processor still in a thread brought the
// kernel of macOS 15.6 down when it reaped that thread.
// `svc`, a refused access and an undefined instruction of the program arrive
// in the vector, `hvc` hands them to the host, and the host reads ESR_EL1,
// FAR_EL1 and ELR_EL1. Measured on an M2: 1.8 microseconds per round trip.
// The host binary needs the entitlement com.apple.security.hypervisor
// (tools/native_app.py signs it). docs/ARM64-APPS.md.
#include <mach/mach_time.h>
#include <Hypervisor/Hypervisor.h>
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "../src/arm64_machine.h"
#include "../src/x86_cpu.h"

namespace
{
using X86::CpuError;
using X86::CpuFault;
using X86::Prot;
using U64 = unsigned long long;

constexpr U64 SpaceBytes = 1ull << 32;
constexpr unsigned PageCount = 1u << 20;
constexpr unsigned HostPage = 16384;
// The intermediate address space (36 bits): slot 0 holds the pool of table
// pages at 256 MiB and the shared host pages from 1 GiB on; slots 1 to 15
// hold one process each.
constexpr U64 PoolIpa = 0x10000000ull, PoolBytes = 0x10000000ull;
constexpr U64 SharedIpa = 0x40000000ull, SharedEnd = 0x100000000ull;
constexpr unsigned Slots = 16;
// The EL1 page, in the upper half of the address space.
constexpr U64 KernelPage = 0xffffffffffff0000ull;
constexpr unsigned StubEnter = 0x800, StubFlush = 0x810;

// Page state.
constexpr unsigned char Mapped = 8, Foreign = 16, Used = 32;

// Stage 1 descriptor bits.
constexpr U64 Valid = 3, AccessFlag = 1ull << 10, InnerShared = 3ull << 8, NotGlobal = 1ull << 11,
              User = 1ull << 6, ReadOnly = 1ull << 7, Uxn = 1ull << 54, Pxn = 1ull << 53;

struct Shared
{
    U64 ipa = 0;
    unsigned references = 0;
};

// Everything the host process has once.
struct World
{
    std::mutex lock;
    bool created = false, failed = false;
    hv_return_t failure = 0;
    unsigned char *pool = nullptr;
    std::vector<unsigned> free_pages;
    unsigned pool_next = 0;
    U64 kernel_table = 0; // IPA of the level 1 table of TTBR1
    bool slot_used[Slots] = {};
    std::unordered_map<unsigned char *, Shared> shared; // by host page of 16 KiB
    std::vector<U64> free_shared;
    U64 shared_next = SharedIpa;
    std::vector<class HvfMachine *> live;
    // The processors that exist, each owned by the thread that made it.
    std::vector<struct Processor *> processors;
    std::condition_variable processors_changed;
    // Set by Shutdown: no processor is made or entered any more.
    std::atomic<bool> closed{false};

    // A zeroed table page of the pool; its index, zero when the pool is full.
    unsigned AllocatePage()
    {
        unsigned index = 0;
        if (!free_pages.empty())
        {
            index = free_pages.back();
            free_pages.pop_back();
        }
        else if ((U64)(pool_next + 1) * 4096 <= PoolBytes)
            index = pool_next++;
        else
            return 0;
        memset(pool + (U64)index * 4096, 0, 4096);
        return index;
    }
    void FreePage(unsigned index) { free_pages.push_back(index); }
    U64 *Table(unsigned index) { return reinterpret_cast<U64 *>(pool + (U64)index * 4096); }
    static U64 PageIpa(unsigned index) { return PoolIpa + (U64)index * 4096; }
    static unsigned PageIndex(U64 descriptor) { return (unsigned)(((descriptor & 0x0000fffffffff000ull) - PoolIpa) / 4096); }

    bool Create()
    {
        if (created) return true;
        if (failed || closed.load()) return false;
        hv_return_t result = hv_vm_create(nullptr);
        if (result != HV_SUCCESS)
        {
            failed = true;
            failure = result;
            fprintf(stderr, "ARM64 machine: hv_vm_create failed (%08x)%s\n", (unsigned)result,
                    result == HV_DENIED ? "; the host binary lacks the entitlement com.apple.security.hypervisor" : "");
            return false;
        }
        pool = static_cast<unsigned char *>(mmap(nullptr, PoolBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0));
        if (pool == MAP_FAILED || hv_vm_map(pool, PoolIpa, PoolBytes, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) != HV_SUCCESS)
        {
            failed = true;
            return false;
        }
        pool_next = 1; // index 0 means "none"
        // The upper half: one page of EL1 code at KernelPage.
        const unsigned l1 = AllocatePage(), l2 = AllocatePage(), l3 = AllocatePage(), code = AllocatePage();
        Table(l1)[3] = PageIpa(l2) | Valid;
        Table(l2)[511] = PageIpa(l3) | Valid;
        Table(l3)[(KernelPage >> 12) & 511] = PageIpa(code) | Valid | AccessFlag | InnerShared | ReadOnly | Uxn;
        kernel_table = PageIpa(l1);
        auto *words = reinterpret_cast<unsigned *>(pool + (U64)code * 4096);
        // The vector: sixteen entries of 128 bytes, each `hvc #entry; eret`.
        for (unsigned entry = 0; entry < 16; ++entry)
        {
            words[entry * 32] = 0xd4000002u | entry << 5;
            words[entry * 32 + 1] = 0xd69f03e0u;
        }
        // Enter the program: eret. After a change of rights: tlbi vmalle1;
        // dsb nsh; isb; eret. Without the dsb the old entry stayed in effect.
        words[StubEnter / 4] = 0xd69f03e0u;
        words[StubFlush / 4] = 0xd508871fu;
        words[StubFlush / 4 + 1] = 0xd503379fu;
        words[StubFlush / 4 + 2] = 0xd5033fdfu;
        words[StubFlush / 4 + 3] = 0xd69f03e0u;
        created = true;
        return true;
    }

    // Host bytes at a place of their own in the intermediate address space,
    // one host page of 16 KiB at a time. Returns the IPA of that host page.
    U64 Share(unsigned char *host_page)
    {
        auto found = shared.find(host_page);
        if (found != shared.end())
        {
            ++found->second.references;
            return found->second.ipa;
        }
        U64 ipa = 0;
        if (!free_shared.empty())
        {
            ipa = free_shared.back();
            free_shared.pop_back();
        }
        else if (shared_next + HostPage <= SharedEnd)
        {
            ipa = shared_next;
            shared_next += HostPage;
        }
        else
            return 0;
        if (hv_vm_map(host_page, ipa, HostPage, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) != HV_SUCCESS)
        {
            free_shared.push_back(ipa);
            return 0;
        }
        shared[host_page] = {ipa, 1};
        return ipa;
    }
    void Unshare(unsigned char *host_page)
    {
        auto found = shared.find(host_page);
        if (found == shared.end() || --found->second.references) return;
        hv_vm_unmap(found->second.ipa, HostPage);
        free_shared.push_back(found->second.ipa);
        shared.erase(found);
    }
    void Shutdown();
    void Close();
    // exit has destroyed the processor of its thread before this.
    ~World() { Close(); }
} world;

class HvfMachine;
// The virtual processor of one host thread.
struct Processor
{
    bool created = false;
    hv_vcpu_t id = 0;
    hv_vcpu_exit_t *exit = nullptr;
    HvfMachine *owner = nullptr;
    // The identity of that machine: a machine that ended on another thread
    // may have left its address to a new one. Mapping changes must
    // not make the live owner look dead before its pending registers are read.
    unsigned long owner_generation = 0;
    // The Registers whose vector registers this processor holds.
    Arm64::Registers *vectors = nullptr;
    Arm64::Registers *general = nullptr;
    unsigned general_pending = 0, general_valid = 0;
    U64 general_value[31] = {};
    U64 general_reads = 0, general_writes = 0, calls = 0;

    // The pause of the machine this processor's counter already stands
    // behind the host's, in microseconds (Arm64::CounterPause).
    unsigned long long counter_pause = 0;
    U64 counter_offset = 0;
    unsigned long generation[Slots] = {};
    // On the thread that owns it only. The world lock is not held.
    void Destroy()
    {
        if (!created) return;
        if (getenv("NEO98_ARM64_REG_STATS"))
            fprintf(stderr, "ARM64 REGISTERS calls=%llu reads=%llu writes=%llu\n", calls, general_reads, general_writes);
        hv_vcpu_destroy(id);
        created = false;
        owner = nullptr;
        vectors = nullptr;
        general = nullptr;
        general_pending = general_valid = 0;
        general_reads = general_writes = calls = 0;
        std::lock_guard<std::mutex> guard(world.lock);
        for (auto it = world.processors.begin(); it != world.processors.end(); ++it)
            if (*it == this) { world.processors.erase(it); break; }
        world.processors_changed.notify_all();
    }
    ~Processor() { Destroy(); }
};
thread_local Processor processor;

// The end of the machine, from the thread that ends or replaces the process:
// its own processor goes at once, a processor of another thread is called out
// of the program and destroyed by its thread, and the VM goes when none is
// left. A thread that does not come back within two seconds keeps its
// processor and the VM stays, which is the state before this function.
void World::Shutdown()
{
    closed.store(true);
    processor.Destroy();
    Close();
}
void World::Close()
{
    closed.store(true);
    std::unique_lock<std::mutex> guard(lock);
    if (!created) return;
    std::vector<hv_vcpu_t> others;
    for (auto *other : processors) others.push_back(other->id);
    if (!others.empty()) hv_vcpus_exit(others.data(), (uint32_t)others.size());
    if (!processors_changed.wait_for(guard, std::chrono::seconds(2), [this] { return processors.empty(); }))
    {
        fprintf(stderr, "ARM64 machine: %zu processors still in use at the end, the VM stays\n", processors.size());
        return;
    }
    const hv_return_t result = hv_vm_destroy();
    if (result != HV_SUCCESS) fprintf(stderr, "ARM64 machine: hv_vm_destroy failed (%08x)\n", (unsigned)result);
    created = false;
    failed = true;
    failure = HV_ERROR;
}

class HvfMachine final : public Arm64::Machine
{
  public:
    unsigned Create()
    {
        std::lock_guard<std::mutex> guard(world.lock);
        if (!world.Create()) return world.failure == (hv_return_t)HV_DENIED ? (unsigned)CpuError::Handle : (unsigned)CpuError::Arch;
        for (unsigned i = 1; i < Slots && !slot; ++i)
            if (!world.slot_used[i]) slot = i;
        if (!slot) return CpuError::NoMemory;
        memory = static_cast<unsigned char *>(mmap(nullptr, SpaceBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0));
        state = static_cast<unsigned char *>(mmap(nullptr, PageCount * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
        if (memory == MAP_FAILED || state == MAP_FAILED) { slot = 0; return CpuError::NoMemory; }
        page_read = state + PageCount;
        page_write = state + PageCount * 2;
        if (hv_vm_map(memory, (U64)slot << 32, SpaceBytes, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) != HV_SUCCESS)
        {
            slot = 0;
            return CpuError::NoMemory;
        }
        table = world.AllocatePage();
        if (!table) { hv_vm_unmap((U64)slot << 32, SpaceBytes); slot = 0; return CpuError::NoMemory; }
        world.slot_used[slot] = true;
        world.live.push_back(this);
        // A slot used before may have entries of its address space number in
        // a translation buffer.
        static std::atomic<unsigned long> generations{1};
        identity = generation = generations.fetch_add(1) + 1;
        return CpuError::Ok;
    }
    ~HvfMachine() override
    {
        if (processor.owner == this)
        {
            processor.owner = nullptr;
            processor.vectors = nullptr;
            processor.general = nullptr;
            processor.general_pending = 0;
        }
        std::lock_guard<std::mutex> guard(world.lock);
        for (auto it = world.live.begin(); it != world.live.end(); ++it)
            if (*it == this) { world.live.erase(it); break; }
        if (slot)
        {
            for (auto &entry : foreign)
                world.Unshare(HostPageOf(entry.second));
            FreeTables();
            hv_vm_unmap((U64)slot << 32, SpaceBytes);
            world.slot_used[slot] = false;
        }
        if (memory && memory != MAP_FAILED) munmap(memory, SpaceBytes);
        if (state && state != MAP_FAILED) munmap(state, PageCount * 3);
    }

    unsigned Map(unsigned address, unsigned size, unsigned prot) override { return MapPages(address, size, prot, nullptr); }
    unsigned MapHost(unsigned address, unsigned size, unsigned prot, void *host) override
    {
        if (!host || ((uintptr_t)host & 4095)) return CpuError::Argument;
        return MapPages(address, size, prot, static_cast<unsigned char *>(host));
    }
    unsigned Unmap(unsigned address, unsigned size) override
    {
        if (!Aligned(address, size)) return CpuError::Argument;
        const unsigned first = address >> 12, count = size >> 12;
        for (unsigned i = 0; i < count; ++i)
            if (!(state[first + i] & Mapped)) return CpuError::NoMemory;
        std::lock_guard<std::mutex> guard(world.lock);
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned page = first + i;
            if (state[page] & Foreign)
            {
                auto found = foreign.find(page);
                if (found != foreign.end())
                {
                    world.Unshare(HostPageOf(found->second));
                    foreign.erase(found);
                }
                state[page] = 0;
            }
            else
                state[page] = Used;
            page_read[page] = page_write[page] = 0;
            SetEntry(page, 0);
        }
        ++generation;
        return CpuError::Ok;
    }
    unsigned Protect(unsigned address, unsigned size, unsigned prot) override
    {
        if (!Aligned(address, size)) return CpuError::Argument;
        const unsigned first = address >> 12, count = size >> 12;
        for (unsigned i = 0; i < count; ++i)
            if (!(state[first + i] & Mapped)) return CpuError::NoMemory;
        std::lock_guard<std::mutex> guard(world.lock);
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned page = first + i;
            state[page] = (unsigned char)((state[page] & ~7u) | (prot & 7));
            Publish(page);
            if (!SetEntry(page, Descriptor(page))) return CpuError::NoMemory;
        }
        ++generation;
        return CpuError::Ok;
    }
    unsigned Read(unsigned address, void *out, unsigned size) override
    {
        auto *p = static_cast<unsigned char *>(out);
        while (size)
        {
            const unsigned page = address >> 12, offset = address & 4095, n = size < 4096 - offset ? size : 4096 - offset;
            if (!(state[page] & Mapped)) return CpuError::ReadUnmapped;
            memcpy(p, HostOf(page) + offset, n);
            p += n;
            size -= n;
            if (address + n < address && size) return CpuError::ReadUnmapped;
            address += n;
        }
        return CpuError::Ok;
    }
    unsigned Write(unsigned address, const void *data, unsigned size) override
    {
        const auto *p = static_cast<const unsigned char *>(data);
        while (size)
        {
            const unsigned page = address >> 12, offset = address & 4095, n = size < 4096 - offset ? size : 4096 - offset;
            if (!(state[page] & Mapped)) return CpuError::WriteUnmapped;
            memcpy(HostOf(page) + offset, p, n);
            if (state[page] & Prot::Exec) sys_icache_invalidate(HostOf(page) + offset, n);
            p += n;
            size -= n;
            if (address + n < address && size) return CpuError::WriteUnmapped;
            address += n;
        }
        return CpuError::Ok;
    }
    void *HostPointer(unsigned address, unsigned size) override
    {
        if (!size) size = 1;
        const unsigned first = address >> 12, last = (unsigned)(((U64)address + size - 1) >> 12);
        if ((U64)address + size > SpaceBytes) return nullptr;
        unsigned char *base = nullptr;
        for (unsigned page = first; page <= last; ++page)
        {
            if (!(state[page] & Mapped)) return nullptr;
            unsigned char *host = HostOf(page);
            if (page == first) base = host;
            else if (host != base + (U64)(page - first) * 4096) return nullptr;
        }
        return base + (address & 4095);
    }
    bool Rights(unsigned address, unsigned &prot) override
    {
        const unsigned char s = state[address >> 12];
        prot = s & 7;
        return s & Mapped;
    }
    void InvalidateCode(unsigned address, unsigned size) override
    {
        while (size)
        {
            const unsigned page = address >> 12, offset = address & 4095, n = size < 4096 - offset ? size : 4096 - offset;
            if (state[page] & Mapped) sys_icache_invalidate(HostOf(page) + offset, n);
            size -= n;
            address += n;
            if (!address) break;
        }
    }
    unsigned char *Memory() override { return memory; }
    const unsigned char *PageRead() override { return page_read; }
    const unsigned char *PageWrite() override { return page_write; }

    void Run(Arm64::Registers &registers, Arm64::Exit &exit) override;
    void SyncGeneral(Arm64::Registers &registers) override
    {
        auto &cpu = processor;
        if (!cpu.created || cpu.owner != this || cpu.owner_generation != identity || cpu.general != &registers) return;
        for (unsigned i = 0; i < 31; ++i)
        {
            const unsigned bit = 1u << i;
            if (!(cpu.general_pending & bit)) continue;
            uint64_t value = 0;
            if (hv_vcpu_get_reg(cpu.id, (hv_reg_t)(HV_REG_X0 + i), &value) != HV_SUCCESS) continue;
            ++cpu.general_reads;
            registers.x[i] = cpu.general_value[i] = value;
            cpu.general_valid |= bit;
            cpu.general_pending &= ~bit;
        }
    }
    void GeneralChanged(Arm64::Registers &registers) override
    {
        auto &cpu = processor;
        if (cpu.general == &registers)
        {
            // A restored RAM context replaces pending physical values.
            // Keep only the physical values already known for write elision.
            cpu.general = nullptr;
            cpu.general_pending = 0;
        }
    }
    void SyncVectors(Arm64::Registers &registers) override
    {
        auto &cpu = processor;
        if (!cpu.created || cpu.vectors != &registers) return;
        for (unsigned i = 0; i < 32; ++i)
        {
            hv_simd_fp_uchar16_t value;
            hv_vcpu_get_simd_fp_reg(cpu.id, (hv_simd_fp_reg_t)(HV_SIMD_FP_REG_Q0 + i), &value);
            memcpy(&registers.v[i], &value, 16);
        }
        uint64_t word = 0;
        hv_vcpu_get_reg(cpu.id, HV_REG_FPCR, &word);
        registers.fpcr = word;
        hv_vcpu_get_reg(cpu.id, HV_REG_FPSR, &word);
        registers.fpsr = word;
        cpu.vectors = nullptr;
    }
    void VectorsChanged(Arm64::Registers &registers) override
    {
        if (processor.vectors == &registers) processor.vectors = nullptr;
    }
    void Slice(unsigned long long nanoseconds) override
    {
        static mach_timebase_info_data_t base;
        if (!base.denom) mach_timebase_info(&base);
        slice_ticks = nanoseconds * base.denom / base.numer;
        slice_retry_ticks = 20000ull * base.denom / base.numer;
        if (nanoseconds && !slice_ticks) slice_ticks = 1;
    }
    void RequestStop() override
    {
        stop_requested.store(true);
        const long running = running_processor.load();
        if (running >= 0)
        {
            hv_vcpu_t id = (hv_vcpu_t)running;
            hv_vcpus_exit(&id, 1);
        }
    }

  private:
    unsigned slot = 0;
    unsigned char *memory = nullptr;
    // state, then the two tables X86::Cpu hands out, one byte per page each.
    unsigned char *state = nullptr, *page_read = nullptr, *page_write = nullptr;
    unsigned table = 0; // level 1 table of TTBR0, a pool page
    unsigned long identity = 0, generation = 0;
    std::unordered_map<unsigned, unsigned char *> foreign; // page -> host bytes
    std::atomic<bool> stop_requested{false};
    // Ticks of mach_absolute_time a Run may last; zero for no limit.
    U64 slice_ticks = 0, slice_retry_ticks = 0;
    std::atomic<long> running_processor{-1};

    static bool Aligned(unsigned address, unsigned size)
    {
        return size && !(address & 4095) && !(size & 4095) && (U64)address + size <= SpaceBytes;
    }
    static unsigned char *HostPageOf(unsigned char *host) { return reinterpret_cast<unsigned char *>((uintptr_t)host & ~(uintptr_t)(HostPage - 1)); }
    unsigned char *HostOf(unsigned page)
    {
        if (state[page] & Foreign)
        {
            auto found = foreign.find(page);
            if (found != foreign.end()) return found->second;
        }
        return memory + ((U64)page << 12);
    }
    // What a page shows in the tables for access in place: only pages whose
    // bytes lie at memory + address.
    void Publish(unsigned page)
    {
        const unsigned char s = state[page];
        const bool plain = (s & Mapped) && !(s & Foreign);
        page_read[page] = plain && (s & Prot::Read) ? 1 : 0;
        page_write[page] = plain && (s & Prot::Write) ? 1 : 0;
    }
    U64 Descriptor(unsigned page)
    {
        const unsigned char s = state[page];
        if (!(s & Mapped) || !(s & 7)) return 0;
        U64 target;
        if (s & Foreign)
        {
            unsigned char *host = foreign[page];
            auto found = world.shared.find(HostPageOf(host));
            if (found == world.shared.end()) return 0;
            target = found->second.ipa + (U64)(host - HostPageOf(host));
        }
        else
            target = ((U64)slot << 32) + ((U64)page << 12);
        U64 descriptor = target | Valid | AccessFlag | InnerShared | NotGlobal | User | Pxn;
        if (!(s & Prot::Write)) descriptor |= ReadOnly;
        if (!(s & Prot::Exec)) descriptor |= Uxn;
        return descriptor;
    }
    // Writes the level 3 entry of a page, making the tables above it. The
    // world lock is held.
    bool SetEntry(unsigned page, U64 descriptor)
    {
        U64 *l1 = world.Table(table);
        U64 &e1 = l1[page >> 18];
        if (!(e1 & 1))
        {
            if (!descriptor) return true;
            const unsigned fresh = world.AllocatePage();
            if (!fresh) return false;
            e1 = World::PageIpa(fresh) | Valid;
        }
        U64 *l2 = world.Table(World::PageIndex(e1));
        U64 &e2 = l2[(page >> 9) & 511];
        if (!(e2 & 1))
        {
            if (!descriptor) return true;
            const unsigned fresh = world.AllocatePage();
            if (!fresh) return false;
            e2 = World::PageIpa(fresh) | Valid;
        }
        world.Table(World::PageIndex(e2))[page & 511] = descriptor;
        return true;
    }
    void FreeTables()
    {
        if (!table) return;
        U64 *l1 = world.Table(table);
        for (unsigned i = 0; i < 4; ++i)
        {
            if (!(l1[i] & 1)) continue;
            U64 *l2 = world.Table(World::PageIndex(l1[i]));
            for (unsigned k = 0; k < 512; ++k)
                if (l2[k] & 1) world.FreePage(World::PageIndex(l2[k]));
            world.FreePage(World::PageIndex(l1[i]));
        }
        world.FreePage(table);
        table = 0;
    }
    unsigned MapPages(unsigned address, unsigned size, unsigned prot, unsigned char *host)
    {
        if (!Aligned(address, size)) return CpuError::Argument;
        const unsigned first = address >> 12, count = size >> 12;
        for (unsigned i = 0; i < count; ++i)
            if (state[first + i] & Mapped) return CpuError::Map;
        std::lock_guard<std::mutex> guard(world.lock);
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned page = first + i;
            if (host)
            {
                unsigned char *bytes = host + (U64)i * 4096;
                if (!world.Share(HostPageOf(bytes))) return CpuError::NoMemory;
                foreign[page] = bytes;
                state[page] = (unsigned char)(Mapped | Foreign | (prot & 7));
            }
            else
            {
                // Map supplies zeroed storage; a page that was used before
                // still holds what its last owner left.
                if (state[page] & Used) memset(memory + ((U64)page << 12), 0, 4096);
                state[page] = (unsigned char)(Mapped | Used | (prot & 7));
            }
            Publish(page);
            if (!SetEntry(page, Descriptor(page))) return CpuError::NoMemory;
        }
        return CpuError::Ok;
    }
    bool Prepare(Processor &cpu);
};

// The virtual processor of this thread, made on its first use.
bool HvfMachine::Prepare(Processor &cpu)
{
    if (world.closed.load())
    {
        cpu.Destroy();
        return false;
    }
    if (cpu.created) return true;
    {
        // Under the lock, so Shutdown either sees this processor or keeps it
        // from being made.
        std::lock_guard<std::mutex> guard(world.lock);
        if (world.closed.load() || hv_vcpu_create(&cpu.id, &cpu.exit, nullptr) != HV_SUCCESS) return false;
        cpu.created = true;
        world.processors.push_back(&cpu);
    }
    // 4 KiB pages, 32 bits in each half, 36 bits of intermediate address,
    // address space numbers of 16 bits taken from TTBR0.
    const U64 tcr = 32ull | 1ull << 8 | 1ull << 10 | 3ull << 12 |
                    32ull << 16 | 1ull << 24 | 1ull << 26 | 3ull << 28 | 2ull << 30 |
                    1ull << 32 | 1ull << 36;
    // MMU and caches on; EL0 may use the cache instructions and read the
    // counter and cache type registers, as on Windows.
    const U64 sctlr = 1ull | 1ull << 2 | 1ull << 12 | 1ull << 11 | 1ull << 20 | 1ull << 22 | 1ull << 23 | 1ull << 28 |
                      1ull << 29 | 1ull << 26 | 1ull << 15 | 1ull << 14;
    bool ok = hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_TCR_EL1, tcr) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_MAIR_EL1, 0xff) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_TTBR1_EL1, world.kernel_table) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_VBAR_EL1, KernelPage) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CPACR_EL1, 3ull << 20) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CNTKCTL_EL1, 3) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_SP_EL1, KernelPage + 0xff0) == HV_SUCCESS;
    ok = ok && hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_SCTLR_EL1, sctlr) == HV_SUCCESS;
    hv_vcpu_set_vtimer_mask(cpu.id, true);
    return ok;
}

void HvfMachine::Run(Arm64::Registers &registers, Arm64::Exit &exit)
{
    exit = {};
    auto &cpu = processor;
    if (!Prepare(cpu))
    {
        exit.event = Arm64::Event::Failed;
        exit.error = CpuError::Resource;
        return;
    }
    stop_requested.store(false);
    bool flush = false;
    if (cpu.owner != this || cpu.owner_generation != identity)
    {
        // Another process ran on this processor: its vector registers go
        // back to it before ours come in, unless it is gone.
        bool alive = false;
        {
            std::lock_guard<std::mutex> guard(world.lock);
            for (auto *machine : world.live) alive = alive || (machine == cpu.owner && machine->identity == cpu.owner_generation);
        }
        if (alive && cpu.vectors) cpu.owner->SyncVectors(*cpu.vectors);
        if (alive && cpu.general) cpu.owner->SyncGeneral(*cpu.general);
        cpu.owner = this;
        cpu.owner_generation = identity;
        cpu.vectors = nullptr;
        cpu.general = nullptr;
        cpu.general_pending = 0;
        hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_TTBR0_EL1, World::PageIpa(table) | (U64)slot << 48);
    }
    if (cpu.generation[slot] != generation)
    {
        cpu.generation[slot] = generation;
        flush = true;
    }
    if (cpu.general != &registers) cpu.general_pending = 0;
    for (unsigned i = 0; i < 31; ++i)
    {
        const unsigned bit = 1u << i;
        if (cpu.general_pending & bit) continue; // unchanged, still in the processor
        if ((cpu.general_valid & bit) && cpu.general_value[i] == registers.x[i]) continue;
        if (hv_vcpu_set_reg(cpu.id, (hv_reg_t)(HV_REG_X0 + i), registers.x[i]) == HV_SUCCESS)
        {
            ++cpu.general_writes;
            cpu.general_value[i] = registers.x[i];
            cpu.general_valid |= bit;
        }
        else cpu.general_valid &= ~bit;
    }
    cpu.general = &registers;
    hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_SP_EL0, registers.sp);
    hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_ELR_EL1, registers.pc);
    hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_SPSR_EL1, registers.pstate & 0xf0000000ull);
    hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_TPIDR_EL0, registers.tpidr);
    if (cpu.vectors != &registers)
    {
        for (unsigned i = 0; i < 32; ++i)
        {
            hv_simd_fp_uchar16_t value;
            memcpy(&value, &registers.v[i], 16);
            hv_vcpu_set_simd_fp_reg(cpu.id, (hv_simd_fp_reg_t)(HV_SIMD_FP_REG_Q0 + i), value);
        }
        hv_vcpu_set_reg(cpu.id, HV_REG_FPCR, registers.fpcr);
        hv_vcpu_set_reg(cpu.id, HV_REG_FPSR, registers.fpsr);
        cpu.vectors = &registers;
    }
    hv_vcpu_set_reg(cpu.id, HV_REG_PC, KernelPage + (flush ? StubFlush : StubEnter));
    hv_vcpu_set_reg(cpu.id, HV_REG_CPSR, 0x3c5);

    // The virtual timer of the processor ends the slice: CNTVCT_EL0 is
    // mach_absolute_time less the offset. The offset is the time the
    // machine stood paused, so the counter a program reads does not jump
    // over a pause.
    if (const unsigned long long pause = Arm64::CounterPause(); pause != cpu.counter_pause)
    {
        static mach_timebase_info_data_t base;
        if (!base.denom) mach_timebase_info(&base);
        const U64 offset = (U64)((unsigned __int128)pause * 1000 * base.denom / base.numer);
        if (hv_vcpu_set_vtimer_offset(cpu.id, offset) == HV_SUCCESS) cpu.counter_offset = offset;
        cpu.counter_pause = pause;
    }
    if (slice_ticks)
    {
        hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CNTV_CVAL_EL0, mach_absolute_time() - cpu.counter_offset + slice_ticks);
        hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CNTV_CTL_EL0, 1);
        hv_vcpu_set_vtimer_mask(cpu.id, false);
    }
    else
    {
        hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CNTV_CTL_EL0, 0);
        hv_vcpu_set_vtimer_mask(cpu.id, true);
    }
    running_processor.store((long)cpu.id);
    for (;;)
    {
        if (world.closed.load())
        {
            exit.event = Arm64::Event::Failed;
            exit.error = CpuError::Resource;
            break;
        }
        if (stop_requested.load())
        {
            exit.event = Arm64::Event::Stopped;
            break;
        }
        const hv_return_t result = hv_vcpu_run(cpu.id);
        if (result != HV_SUCCESS)
        {
            exit.event = Arm64::Event::Failed;
            exit.error = CpuError::Resource;
            break;
        }
        if (cpu.exit->reason == HV_EXIT_REASON_CANCELED)
        {
            if (!stop_requested.load()) continue; // a stop meant for an earlier run
            exit.event = Arm64::Event::Stopped;
            break;
        }
        if (cpu.exit->reason == HV_EXIT_REASON_VTIMER_ACTIVATED)
        {
            // The slice is used up. The timer is masked now; it is armed
            // again by the next Run.
            uint64_t cpsr = 0;
            hv_vcpu_get_reg(cpu.id, HV_REG_CPSR, &cpsr);
            if (!slice_ticks) continue;
            if ((cpsr & 0xf) != 0)
            {
                // In the entry stub or in the vector, a few instructions
                // from the program or from its call: ask again right behind.
                hv_vcpu_set_sys_reg(cpu.id, HV_SYS_REG_CNTV_CVAL_EL0, mach_absolute_time() - cpu.counter_offset + slice_retry_ticks);
                hv_vcpu_set_vtimer_mask(cpu.id, false);
                continue;
            }
            exit.event = Arm64::Event::Slice;
            break;
        }
        if (cpu.exit->reason != HV_EXIT_REASON_EXCEPTION)
        {
            exit.event = Arm64::Event::Failed;
            exit.error = CpuError::Exception;
            break;
        }
        const U64 syndrome = cpu.exit->exception.syndrome;
        uint64_t esr = 0, far = 0;
        hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_ESR_EL1, &esr);
        hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_FAR_EL1, &far);
        exit.syndrome = (unsigned)esr;
        // Anything but the `hvc` of the vector entry for a synchronous
        // exception of the program is a failure of the machine itself.
        if ((syndrome >> 26) != 0x16 || (syndrome & 0xffff) != 8)
        {
            exit.event = Arm64::Event::Failed;
            exit.error = CpuError::Exception;
            exit.syndrome = (unsigned)syndrome;
            exit.address = cpu.exit->exception.virtual_address;
            break;
        }
        const unsigned cause = (unsigned)(esr >> 26) & 63;
        if (cause == 0x15)
            exit.event = Arm64::Event::Call;
        else if (cause == 0x20 || cause == 0x24)
        {
            exit.event = Arm64::Event::Fault;
            exit.address = far;
            exit.size = 4;
            const bool mapped = !(far >> 32) && (state[far >> 12] & Mapped);
            if (cause == 0x20) exit.kind = mapped ? CpuFault::FetchProtected : CpuFault::FetchUnmapped;
            else if (esr & 0x40) exit.kind = mapped ? CpuFault::WriteProtected : CpuFault::WriteUnmapped;
            else exit.kind = mapped ? CpuFault::ReadProtected : CpuFault::ReadUnmapped;
        }
        else
            exit.event = Arm64::Event::Illegal;
        break;
    }
    running_processor.store(-1);
    // The entry/exception stubs use no general registers. At an API call
    // the bridge needs arguments and LR only; a context consumer fetches
    // the rest explicitly. Interruptions and faults materialize all of them.
    if (exit.event == Arm64::Event::Call) ++cpu.calls;
    cpu.general_valid = 0;
    cpu.general_pending = 0x7fffffffu;
    const unsigned needed = exit.event == Arm64::Event::Call ? 0x400000ffu : 0x7fffffffu;
    for (unsigned i = 0; i < 31; ++i)
    {
        const unsigned bit = 1u << i;
        if (!(needed & bit)) continue;
        uint64_t word = 0;
        if (hv_vcpu_get_reg(cpu.id, (hv_reg_t)(HV_REG_X0 + i), &word) != HV_SUCCESS) continue;
        ++cpu.general_reads;
        registers.x[i] = cpu.general_value[i] = word;
        cpu.general_valid |= bit;
        cpu.general_pending &= ~bit;
    }
    uint64_t value = 0;
    hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_SP_EL0, &value);
    registers.sp = value;
    hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_TPIDR_EL0, &value);
    registers.tpidr = value;
    if (exit.event == Arm64::Event::Stopped || exit.event == Arm64::Event::Slice || exit.event == Arm64::Event::Failed)
    {
        // The program was interrupted between two instructions (or never
        // entered): the processor holds its state itself.
        uint64_t pc = 0, cpsr = 0;
        hv_vcpu_get_reg(cpu.id, HV_REG_PC, &pc);
        hv_vcpu_get_reg(cpu.id, HV_REG_CPSR, &cpsr);
        if ((cpsr & 0xf) == 0)
        {
            registers.pc = pc;
            registers.pstate = cpsr & 0xf0000000ull;
        }
        // Still in the entry stub: ELR_EL1 and SPSR_EL1 hold what was loaded.
    }
    else
    {
        hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_ELR_EL1, &value);
        registers.pc = value;
        hv_vcpu_get_sys_reg(cpu.id, HV_SYS_REG_SPSR_EL1, &value);
        registers.pstate = value & 0xf0000000ull;
    }
    // At a call the vector registers stay in the processor until somebody
    // asks; every other event is rare and hands them over at once.
    if (exit.event != Arm64::Event::Call) SyncVectors(registers);
    if (world.closed.load()) cpu.Destroy();
}

unsigned CreateMachine(Arm64::Machine *&machine)
{
    machine = nullptr;
    auto *made = new HvfMachine;
    const unsigned error = made->Create();
    if (error)
    {
        delete made;
        return error;
    }
    machine = made;
    return CpuError::Ok;
}

struct Registration
{
    Registration() { Arm64::RegisterMachine(CreateMachine, [] { world.Shutdown(); }); }
} registration;
} // namespace
