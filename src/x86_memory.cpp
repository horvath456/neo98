// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_interlocked.h"
#include "win98_virtual.h"
#include "win98_resource_write.h"
#include "x86_app.h"
#include "pc_bios.h"
#include "system_vm.h"
#include "win98_ne_image.h"
#include <stdio.h>
#include <stdlib.h>
#include "win98_memory_status.h"
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#else
#include <circle/memory.h>
#endif
#include <string.h>
namespace X86
{
namespace
{
// Native-only address reservations: future thread stacks, service modules and TEBs.
// Every one of them lies outside the window Win98 keeps for system modules, so
// an original DLL still reaches its preferred base. The service modules used to
// have a second reservation overlapping COMDLG32.DLL
// (SizeOfImage 2D000h), which pushed the image away from its preferred base.
// Above 71000000h only what is in use is reserved: the native procedure
// tokens and the marks of 16-bit procedures (71000000h..7101FFFFh), and from
// 71E00000h the system page, the TEBs, the ARM64 area, COMDLG32 and the
// handles of the service modules. Between them Win98 has ordinary private
// space, and Microsoft DLLs have their bases there: MSIDLE.DLL of IE 5.01
// wants 711D0000h, and only at that base is its shared section .shrdata one
// for the whole session.
constexpr unsigned reserved_ranges[][2] = {
    {0x20000000, 0x22200000}, {0x71000000, 0x71020000}, {0x71e00000, 0x72200000}};
} // namespace
bool Application::ConfigureMemory()
{
    for (const auto &r : reserved_ranges)
        if (!virtual_memory.Exclude(r[0], r[1] - r[0]))
        {
            Fail("Cannot reserve native service address ranges");
            return false;
        }
    return true;
}
bool Application::EnsureSystemVmMemory()
{
    if (system_vm_memory) return true;
    // One backing for flat windows, Win16 selectors and DOS allocations.
    // 64-KiB alignment also satisfies FEX's larger host-page mapping.
#if defined(__APPLE__) || defined(__linux__)
    void *storage = nullptr;
    if (posix_memalign(&storage, 0x10000, 0x110000)) return false;
    system_vm_storage = static_cast<unsigned char *>(storage);
    system_vm_memory = system_vm_storage;
#else
    system_vm_storage = new unsigned char[0x120000];
    if (!system_vm_storage) return false;
    system_vm_memory = reinterpret_cast<unsigned char *>(
        (reinterpret_cast<unsigned long long>(system_vm_storage) + 0xffff) & ~0xffffull);
#endif
    memset(system_vm_memory, 0, 0x110000);
    memset(system_vm_memory + 0xc0000, 0xff, 0x30000);
    unsigned char page[4096];
    for (unsigned offset = 0; offset < PcBios::Size; offset += sizeof page)
    {
        PcBios::Page(offset, page);
        memcpy(system_vm_memory + PcBios::Base + offset, page, sizeof page);
    }
    const unsigned short ports[] = {0x3f8, 0x2f8, 0, 0, 0x378, 0, 0};
    memcpy(system_vm_memory + 0x400, ports, sizeof ports);
    const unsigned short equipment = 2u << 9 | 1u << 14 | 1u << 1 | 2u << 4, memory = 640;
    memcpy(system_vm_memory + 0x410, &equipment, 2);
    memcpy(system_vm_memory + 0x413, &memory, 2);
    const unsigned flags = SystemVm::WinFlags;
    memcpy(system_vm_memory + SystemVm::KernelBase + SystemVm::WinFlagsOffset, &flags, sizeof flags);
    // Rollcage's null participant context lands inside KERNEL's
    // instruction decoder tables. These are static bytes, not selectors or
    // boot-time handles. Copy the table, never the segment's relocation chains.
    unsigned char *bytes = nullptr;
    unsigned length = 0, error = 0;
    if (!Win16ReadFile("C:\\WINDOWS\\SYSTEM\\KRNL386.EXE", bytes, length))
    {
        Win98Ne::Image kernel;
        Win98Ne::Segment data;
        if (kernel.Parse(bytes, length, error) &&
            kernel.SegmentAt(kernel.AutoDataSegment(), data) && !data.Code() &&
            data.file_bytes >= SystemVm::DecoderEnd)
            memcpy(system_vm_memory + SystemVm::KernelBase + SystemVm::DecoderFirst,
                bytes + data.file_offset + SystemVm::DecoderFirst, SystemVm::DecoderEnd - SystemVm::DecoderFirst);
        delete[] bytes;
    }
    return true;
}
bool Application::MapSystemBios()
{
    // Physical system-VM windows, deliberately outside VirtualAlloc metadata
    // (Win98 VirtualQuery reports MEM_FREE). Keep the flat null page unmapped.
    return EnsureSystemVmMemory() &&
        Check(cpu->MapHost(PcBios::Base, PcBios::Size, Prot::All, system_vm_memory + PcBios::Base)) &&
        Check(cpu->MapHost(SystemVm::KernelFirst, SystemVm::KernelEnd - SystemVm::KernelFirst,
                          Prot::Read | Prot::Write, system_vm_memory + SystemVm::KernelFirst));
}
bool Application::MapProcessHeap()
{
    // Under Win98 the loader puts the image at its preferred base first. An
    // EXE without relocations cannot move at all, so the process heap is what
    // gives way: it is created afterwards wherever the private arena from
    // 400000h upwards still has room. 44 programs of the collection carry the
    // linker default 10000000h as their EXE base; see
    heap_base = virtual_memory.FindPrivate(HeapArenaBytes);
    if (!heap_base)
    {
        Fail("No free private address range for the process heap");
        return false;
    }
    next_alloc = heap_base;
    heap_end = heap_base + HeapArenaBytes;
    heap_total = HeapArenaBytes;
    return Map(heap_base, HeapArenaBytes, 3);
}
// Makes room for a contiguous `required` bytes at next_alloc: at least
// 16 MiB more behind the current block when free, a complete new block
// elsewhere otherwise, as
// the process heap of Win98 adds segments. Wonderland Adventures keeps over
// 32 MiB of DirectDraw system memory surfaces, which DDRAW takes from
// LocalAlloc. What is left of the old block becomes a free block.
bool Application::GrowHeap(unsigned required, bool relocate)
{
    const unsigned tail = heap_end - next_alloc;
    if (required <= tail) return true;
    const unsigned step = 0x1000000;
    unsigned grow = (required - tail + step - 1) & ~(step - 1);
    if (!grow || heap_total + grow > HeapArenaLimit || heap_total + grow < heap_total) return false;
    unsigned base = heap_end;
    if (!virtual_memory.Available(base, grow))
    {
        if (!relocate) return false;
        // The old tail is not adjacent to a new segment. Counting it toward
        // the request allocated only 16 MiB for Half-Life's 32 MiB GlobalAlloc.
        grow = (required + step - 1) & ~(step - 1);
        if (!grow || grow > HeapArenaLimit - heap_total) return false;
        base = virtual_memory.FindPrivate(grow);
    }
    if (!base || !Map(base, grow, 3)) return false;
    if (base != heap_end)
    {
        const unsigned rest = heap_end - next_alloc;
        if (rest >= 16)
            for (auto &b : AllocationBlocks())
                if (!b.address)
                {
                    b = {next_alloc, rest, false};
                    break;
                }
        next_alloc = base;
    }
    heap_end = base + grow;
    heap_total += grow;
    if (log)
    {
        char text[96];
        snprintf(text, sizeof text, "Heap arena grown to %u MiB, block at %08x", heap_total >> 20, base);
        log(text);
    }
    return true;
}
bool Application::AvailableImageAddress(unsigned base, unsigned size)
{
    const auto end = static_cast<unsigned long long>(base) + size;
    if (base < 0x10000 || end > 0x100000000ull)
        return false;
    return virtual_memory.Available(base, size);
}
struct Application::MemoryBackend final : VirtualMemory::Backend
{
    Application &app;
    Application &errors;
    explicit MemoryBackend(Application &a) : app(a), errors(a) {}
    MemoryBackend(Application &a, Application &caller) : app(a), errors(caller) {}
    bool Available(unsigned base, unsigned size) override { return app.AvailableImageAddress(base, size); }
    // VirtualMemory hands down what the guest sees, which always carries the
    // execute right (virtual_memory.cpp, Commit and Protect). Unicorn gets it
    // only where code lives.
    bool Map(unsigned base, unsigned size, unsigned permissions) override
    {
        if (app.cpu->Map(base, size, app.HostProtection(base, size, permissions & ~(unsigned)Prot::Exec)) !=
            CpuError::Ok)
            return false;
        app.KeepExecutable(base, size, permissions & ~(unsigned)Prot::Exec);
        return true;
    }
    bool Unmap(unsigned base, unsigned size) override
    {
        app.execute_pages.Remove(base, size);
        return app.Check(app.cpu->Unmap(base, size));
    }
    bool Protect(unsigned base, unsigned size, unsigned permissions) override
    {
        if (app.cpu->Protect(base, size,
                           app.HostProtection(base, size, permissions & ~(unsigned)Prot::Exec)) != CpuError::Ok)
            return false;
        app.KeepExecutable(base, size, permissions & ~(unsigned)Prot::Exec);
        return true;
    }
    void Error(unsigned code) { errors.last_error = code; }
    // The VMM write-watch flag (0x200000) rides along with the protection
    // bits; the native space tracks watched reservations separately.
    unsigned Reserve(unsigned page, unsigned pages, unsigned flags)
    {
        if (page == 0x80060000 || (page >= 0x80000 && page < 0xbfc00))
            return app.ReserveSharedVirtual(page, pages, flags & ~0x200000u);
        return app.virtual_memory.Reserve(*this, page, pages, flags & ~0x200000u);
    }
    unsigned Commit(unsigned page, unsigned pages, unsigned pager, unsigned handle, unsigned flags)
    {
        if (pager == 1 && !handle && app.IsSharedVirtual(page << 12))
            return app.CommitSharedVirtual(page, pages, flags & ~0x200000u);
        return pager == 1 && !handle ? app.virtual_memory.Commit(*this, page, pages, flags & ~0x200000u) : 0;
    }
    unsigned Release(unsigned base, unsigned)
    {
        const auto result = app.IsSharedVirtual(base) ? app.ReleaseSharedVirtual(base)
                                                    : app.virtual_memory.Release(*this, base);
        if (!result)
            Error(487);
        return result;
    }
    unsigned Decommit(unsigned page, unsigned pages, unsigned)
    {
        const auto result = app.IsSharedVirtual(page << 12) ? app.DecommitSharedVirtual(page, pages)
                                                          : app.virtual_memory.Decommit(*this, page, pages);
        if (!result)
            Error(487);
        return result;
    }
};
bool Application::RepairResourceWrite(unsigned address)
{
    struct Environment
    {
        Application &app;
        bool Query(unsigned at, unsigned &allocation, unsigned &protection)
        {
            VirtualMemory::Info info{};
            if (!app.virtual_memory.Query(at, info)) return false;
            allocation = info.allocation; protection = info.protection;
            return true;
        }
        unsigned ResourceDirectory(unsigned allocation, unsigned &size)
        {
            // resolves the allocation base through the loaded-module
            // table. A raw LOAD_LIBRARY_AS_DATAFILE mapping is not an image.
            for (unsigned i = 0; i < app.module_watermark; ++i)
            {
                const auto &m = app.modules[i];
                if (!m.pe || m.pe->Arm64() || m.handle != allocation) continue;
                size = m.pe->ResourceDirectorySize();
                return m.pe->ResourceDirectory();
            }
            return 0;
        }
        bool MakeWritable(unsigned at)
        {
            // Windows 98 uses the internal VWIN32 permission service with
            // the static-image flag, bypassing VirtualProtect's restrictions.
            // Image bytes already have private backing in this process.
            MemoryBackend backend(app);
            return app.virtual_memory.Protect(backend, at, 1, 0x60000) != ~0u;
        }
    } environment{*this};
    return Win98ResourceWrite::Repair(environment, address);
}
unsigned Application::ReserveMappingPages(unsigned page, unsigned pages, unsigned flags)
{
    MemoryBackend e(*this);
    return virtual_memory.ReserveMapping(e, page, pages, flags);
}
unsigned Application::CommitMappingPages(unsigned page, unsigned pages, unsigned flags)
{
    MemoryBackend e(*this);
    return virtual_memory.CommitMapping(e, page, pages, flags);
}
unsigned Application::ReleaseMappingPages(unsigned base)
{
    MemoryBackend e(*this);
    return virtual_memory.ReleaseMapping(e, base);
}
unsigned Application::ReserveHeapPages(unsigned arena, unsigned pages)
{
    MemoryBackend e(*this);
    return virtual_memory.ReserveHeap(e, arena, pages);
}
unsigned Application::CommitHeapPages(unsigned page, unsigned pages)
{
    MemoryBackend e(*this);
    return virtual_memory.CommitHeap(e, page, pages);
}
unsigned Application::DecommitHeapPages(unsigned page, unsigned pages)
{
    MemoryBackend e(*this);
    return virtual_memory.DecommitHeap(e, page, pages);
}
unsigned Application::ReleaseHeapPages(unsigned base)
{
    MemoryBackend e(*this);
    return virtual_memory.ReleaseHeap(e, base);
}
bool Application::Unmap(unsigned address, unsigned size)
{
    execute_pages.Remove(address, size);
    if (!Check(cpu->Unmap(address, size)))
        return false;
    virtual_memory.Forget(address, size);
    return true;
}
#include "x86_process_memory.inc"
unsigned Application::MemoryApi(ApiName name, const unsigned *a)
{
    if (!strncmp(name, "Interlocked", 11))
    {
        // Windows fibers cannot switch during this read/modify/write. A future
        // SMP process backend must supply a real atomic memory operation here.
        struct AtomicMemory
        {
            Application &app;
            unsigned Read32(unsigned p) { return app.Read32(p); }
            void Write32(unsigned p, unsigned v) { app.Write32(p, v); }
        } memory{*this};
        if (API_IS(name,"InterlockedExchange"))
            return Win98Interlocked::Exchange(memory, a[0], a[1]);
        if (API_IS(name,"InterlockedCompareExchange"))
            return Win98Interlocked::CompareExchange(memory, a[0], a[1], a[2]);
        if (API_IS(name,"InterlockedIncrement"))
            return Win98Interlocked::Increment(memory, a[0]);
        if (API_IS(name,"InterlockedDecrement"))
            return Win98Interlocked::Decrement(memory, a[0]);
        if (API_IS(name,"InterlockedExchangeAdd"))
            return Win98Interlocked::ExchangeAdd(memory, a[0], a[1]);
    }
    MemoryBackend e(*this);
    switch (name.id)
    {
    API_CASE("VirtualAlloc")
    {
        // MEM_COMMIT without MEM_RESERVE inside a view of a section, the
        // pages of a SEC_RESERVE section.
        if (a[0] && (a[2] & 0x3000) == 0x1000)
            if (const unsigned view = MappedViewCommit(a[0], a[1])) return view;
        const unsigned result = Win98Virtual::Allocate(e, a[0], a[1], a[2], a[3]);
        // The original VMM translation drops PAGE_EXECUTE (Win98Virtual::Protection), because the i386 of that era has no execute-disable bit.
        // Here the wish is still worth keeping: a program that asks for
        // executable pages gets them without the detour of a fetch fault.
        if (result && (a[2] & 0x1000) && (a[3] & 0xf0)) MarkGuestCode(result, a[1]);
        // MEM_WRITE_WATCH reservations are tracked natively for GetWriteWatch.
        if (result && (a[2] & 0x202000) == 0x202000 && !RegisterWriteWatch(result, ((a[1] + 4095) >> 12)))
        {
            unsigned release[] = {result, 0, 0x8000};
            Win98Virtual::Free(e, release[0], release[1], release[2]);
            last_error = 8;
            return 0;
        }
        return result;
    }
    API_CASE("VirtualFree")
    {
        const unsigned result = Win98Virtual::Free(e, a[0], a[1], a[2]);
        if (result && a[2] == 0x8000) ReleaseWriteWatch(a[0]);
        return result;
    }
    API_CASE("VirtualQuery")
    {
        VirtualMemory::Info info = {};
        if (a[2] < sizeof info || (!QuerySessionDib(a[0], info) && !virtual_memory.Query(a[0], info)))
            return 0;
        return Store(a[1], &info, sizeof info) ? sizeof info : 0;
    }
    API_CASE("VirtualProtect")
    {
        // Current-process protection change; the original permits a null old-protection pointer.
        if (a[1] > 0x7fc00000 || a[0] > 0xbfffffff)
        {
            last_error = 487;
            return 0;
        }
        if (a[3])
            Read32(a[3]); // Public probes readable output before dispatch.
        // System DLLs in the shared arena retain their loader permissions.
        // Private headers and sections, including MEM_SHARED sections, may
        // change protection (native image_protection/repertoire_pe probes).
        if (ImageRightsFixed(a[0], a[1] ? a[1] : 1))
        {
            last_error = 487;
            return 0;
        }
        if (state == Failed)
            return 0;
        const auto flags = Win98Virtual::Protection(e, a[2]);
        if (flags == ~0u)
            return 0;
        const auto previous = IsSharedVirtual(a[0]) ? ProtectSharedVirtual(a[0], a[1], flags)
                                                  : virtual_memory.Protect(e, a[0], a[1], flags);
        if (previous == ~0u)
        {
            last_error = 87;
            return 0;
        }
        // As in VirtualAlloc: an explicit PAGE_EXECUTE wish gets the right at
        // once, everything else on first use.
        if (a[2] & 0xf0) MarkGuestCode(a[0], a[1] ? a[1] : 1);
        const auto result = Win98Virtual::PageProtection(previous);
        if (a[3])
            Write32(a[3], result);
        return result;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
#include "x86_memory_status.inc"
} // namespace X86
