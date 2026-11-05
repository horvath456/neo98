// SPDX-License-Identifier: GPL-2.0-or-later
// Native memory and synchronization providers for the original Win98 heap cores.
#include "win98_heap_blocks.h"
#include "win98_heap_create.h"
#include "x86_app.h"
#include "host_devices.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::HeapSystem
{
    using U = unsigned;
    Application &app;
    Win98HeapCreate::State creation{0, true};
    // Heaps of one process. Win98 gives HeapCreate no small limit; every DLL
    // built with the static C runtime makes one, and iernonce's RunOnceEx run
    // after IE 5.01 had more than 32 alive when LMRT.DLL came.
    U heaps[256]{}, process_heap = 0;
    struct Region
    {
        U base = 0, lock_client = 0, lock_object = 0;
        bool global_lock = false;
        // A heap with HEAP_SHARED takes its pages from the session arena, so
        // its base is the same in every process.
        bool session = false;
    } regions[1024];
    explicit HeapSystem(Application &a) : app(a) {}
    void Error(U value) { app.last_error = value; }
    U Read8(U address)
    {
        // Mutex object tokens name native records, not x86 memory pages.
        if (const auto *mutex = app.FindCritical(address))
            return mutex->data.type & 255;
        unsigned char value = 0;
        app.Read(address, &value, sizeof value);
        return value;
    }
    U Read16(U address)
    {
        unsigned short value = 0;
        app.Read(address, &value, sizeof value);
        return value;
    }
    U Read32(U address) { return app.Read32(address); }
    void Write8(U address, U value)
    {
        const unsigned char byte = value;
        app.Store(address, &byte, sizeof byte);
    }
    void Write16(U address, U value)
    {
        const unsigned short word = value;
        app.Store(address, &word, sizeof word);
    }
    void Write32(U address, U value) { app.Write32(address, value); }
    void Zero(U address, U size)
    {
        const unsigned char zero[4096]{};
        while (size && app.state != Failed)
        {
            const U count = size < sizeof zero ? size : sizeof zero;
            app.Store(address, zero, count);
            address += count;
            size -= count;
        }
    }
    void Copy(U destination, U source, U size)
    {
        unsigned char buffer[4096];
        while (size && app.state != Failed)
        {
            const U count = size < sizeof buffer ? size : sizeof buffer;
            if (!app.Read(source, buffer, count) || !app.Store(destination, buffer, count))
                break;
            source += count;
            destination += count;
            size -= count;
        }
    }
    U Reserve(U arena, U pages, U flags)
    {
        // PR_STATIC is implemented; the additional reservation mode requested
        // by public HeapCreate flag 80h is not yet a native provider capability.
        if (flags != 0x10)
            return ~0u;
        for (auto &region : regions)
            if (!region.base)
            {
                // HeapCreate turns HEAP_SHARED into the arena tag
                // 80060000h. That reservation belongs to the session, because
                // the heap it carries has to be the same heap in every process.
                if (arena == 0x80060000u)
                {
                    const U base = app.ReserveSessionHeap(pages);
                    if (base != ~0u)
                    {
                        region.base = base;
                        region.session = true;
                    }
                    return base;
                }
                const U base = app.ReserveHeapPages(arena, pages);
                if (base != ~0u)
                    region.base = base;
                return base;
            }
        return ~0u;
    }
    Region *SessionRegion(U address)
    {
        for (auto &region : regions)
            if (region.session && region.base == address)
                return &region;
        return nullptr;
    }
    U CommitPages(U page, U count, U pager, U key, U flags)
    {
        if ((pager != 1 && pager != 2) || key || flags != 0x20060000)
        {
            app.Fail("Unsupported original heap pager request");
            return 0;
        }
        // A commit inside a session block reaches every process that holds the
        // block, because the pages of the shared area are one page table in
        // Win98.
        if (app.SessionHeapAddress(page << 12))
            return app.CommitSessionHeap(page, count);
        // Fresh backing is zeroed for both pagers. Pager 2's uninitialized bytes
        // may be zero; already committed pages keep their contents.
        return app.CommitHeapPages(page, count);
    }
    U Commit(U page, U count, U flags) { return Win98HeapBlocks::Commit(*this, page, count, flags); }
    U Decommit(U page, U count, U flags)
    {
        if (flags != 0x20000000)
        {
            app.Fail("Unsupported original heap decommit request");
            return 0;
        }
        // A decommit inside a session block takes the pages away from every
        // process that holds it, as one page table would.
        if (app.SessionHeapAddress(page << 12))
            return app.DecommitSessionHeap(page, count);
        return app.DecommitHeapPages(page, count);
    }
    U Release(U base, U flags)
    {
        if (flags != 0x10)
            return 0;
        // HeapDestroy gives every region back with _PageFree, for
        // a shared heap as well; KERNEL32 counts no processes. What keeps MPR
        // alive after it destroys its own shared heap and reads into it again
        // is the VMM, and NEO98 answers that with the session block: the pages
        // stay mapped and the linear range is never handed out again.
        if (auto *region = SessionRegion(base))
        {
            if (region->lock_object && app.FindCritical(region->lock_object))
                DeleteHeapLock(region->lock_object);
            *region = {};
            app.RetireSessionHeap(base);
            return 1;
        }
        for (auto &region : regions)
            if (region.base == base)
            {
                // Also covers initialization failing after creating its lock.
                if (region.lock_object && app.FindCritical(region.lock_object))
                    DeleteHeapLock(region.lock_object);
                const U result = app.ReleaseHeapPages(base);
                if (result)
                    region = {};
                return result;
            }
        return 0;
    }
    U InitializeHeap(U heap, U base, U size, U flags)
    {
        return Win98HeapCreate::Initialize(*this, creation, heap, base, size, flags);
    }
    void InsertFree(U heap, U block, U size, U flags)
    {
        Win98HeapCreate::InsertFree(*this, heap, block, size, flags);
    }
    void InitializeCriticalSection(U address)
    {
        const U args[] = {address};
        app.CriticalApi("InitializeCriticalSection", args);
        for (auto &region : regions)
            if (region.base + 0x50 == address)
            {
                region.lock_client = address;
                region.lock_object = Read32(address + 4);
                return;
            }
        app.Fail("Heap lock has no native reservation");
    }
    void InitializeEarlyLock(U) { app.Fail("Early boot heap lock reached the application provider"); }
    void MakeCriticalSectionGlobal(U address)
    {
        for (auto &region : regions)
            if (region.lock_client == address && app.FindCritical(Read32(address + 4)))
            {
                const U arguments[] = {address};
                app.CriticalApi("MakeCriticalSectionGlobal", arguments);
                if (app.state == Failed) return;
                region.global_lock = true;
                return;
            }
        app.Fail("Invalid shared heap critical section");
    }
    void EnterCritical() { app.CrtLock(true); }
    void LeaveCritical() { app.CrtLock(false); }
    void EnterKernel() { EnterCritical(); }
    void LeaveKernel() { LeaveCritical(); }
    void Lock(U object) { MutexOperation(object, "EnterCriticalSection"); }
    void Unlock(U object) { MutexOperation(object, "LeaveCriticalSection"); }
    void DeleteHeapLock(U object) { app.ReleaseHeapMutex(object); }
    void ClosePrimaryLock(U object) { DeleteHeapLock(object); }
    void MutexOperation(U object, const char *operation)
    {
        const auto *mutex = app.FindCritical(object);
        if (!mutex || !mutex->data.client)
        {
            app.Fail("Invalid native heap mutex");
            return;
        }
        const U args[] = {mutex->data.client};
        app.CriticalApi(operation, args);
    }
    U PrimaryHeap() const { return process_heap; }
    U ProcessHeap() const { return process_heap; }
    U CompatFlags() const { return 0; } // No per-application compatibility database yet.
    void RaiseNoMemory(U bytes)
    {
        char message[96];
        snprintf(message, sizeof message, "Unhandled heap exception c0000017 allocating %u bytes", bytes);
        app.Fail(message);
    }
    U ValidateHeap(U heap, U block, U flags) { return Win98HeapBlocks::Validate(*this, heap, block, flags); }
    void UnlockHeap(U heap, U flags) { Win98HeapBlocks::Unlock(*this, heap, flags); }
    U Create(U flags, U initial, U maximum)
    {
        for (auto &heap : heaps)
            if (!heap)
            {
                heap = Win98HeapCreate::Create(*this, creation, flags, initial, maximum);
                return heap;
            }
        if (app.log) app.log("HeapCreate failed: the heap table of the process is full");
        Error(8);
        return 0;
    }
};
// Internal allocator on the process heap at PDB+18h, shared by
// fixed Local/GlobalAlloc and the loader. TLS allocation keeps
// the default zero flag 40h; Local/GlobalAlloc supplies its own flags.
unsigned Application::ProcessHeapBlock(unsigned bytes, unsigned flags)
{
    if (!heap_system)
        heap_system = new HeapSystem(*this);
    if (!heap_system)
    {
        last_error = 8;
        return 0;
    }
    auto &e = *heap_system;
    if (!e.process_heap)
        e.process_heap = e.Create(0, 0, 0);
    if (!e.process_heap)
        return 0;
    return Win98HeapBlocks::Allocate(e, e.process_heap, bytes, flags);
}
void Application::FreeProcessHeapBlock(unsigned address)
{
    if (heap_system && heap_system->process_heap && address)
        Win98HeapBlocks::HeapFree(*heap_system, heap_system->process_heap, 0, address);
}
void Application::FreeHeaps()
{
    if (!heap_system)
        return;
    // Teardown also releases partially initialized regions after a guest fault.
    // Public HeapDestroy uses the original region-chain and private-list algorithm.
    // A heap with HEAP_SHARED belongs to the session and outlives this process;
    // only HeapDestroy retires it.
    for (const auto &region : heap_system->regions)
        if (region.base && !region.session)
            heap_system->Release(region.base, 0x10);
    delete heap_system;
    heap_system = nullptr;
}
unsigned Application::HeapApi(ApiName name, const unsigned *a)
{
    // This is the actual local Win98 export, not the internal allocator validator.
    if (API_IS(name,"HeapValidate"))
    {
        last_error = 120;
        return ~0u;
    }
    if (!heap_system)
        heap_system = new HeapSystem(*this);
    if (!heap_system)
    {
        last_error = 8;
        return 0;
    }
    auto &e = *heap_system;
    if (API_IS(name,"GetProcessHeap"))
    {
        if (!e.process_heap)
            e.process_heap = e.Create(0, 0, 0);
        return e.process_heap;
    }
    if (API_IS(name,"HeapCreate"))
        return e.Create(a[0], a[1], a[2]);
    unsigned *slot = nullptr;
    for (auto &heap : e.heaps)
        if (heap && heap == a[0])
        {
            slot = &heap;
            break;
        }
    // A shared heap another process created. Its handle travels in a section
    // with IMAGE_SCN_MEM_SHARED, and the heap itself is a session block, so
    // this process only has to learn about it: MPRSERV allocates on the heap
    // whose handle MPR stored in the shared section.
    if (!slot && AdoptSessionHeap(a[0]))
        for (auto &heap : e.heaps)
            if (!heap)
            {
                heap = a[0];
                slot = &heap;
                for (auto &region : e.regions)
                    if (!region.base)
                    {
                        region.base = a[0];
                        region.session = true;
                        break;
                    }
                break;
            }
    // Guard the original core's heap pointer access at the native provider boundary.
    if (!slot || !virtual_memory.Access(a[0], 0x74, false))
    {
        last_error = 87;
        return 0;
    }
    if (API_IS(name,"HeapDestroy"))
    {
        const unsigned result = Win98HeapCreate::Destroy(e, e.creation, a[0]);
        if (result)
            *slot = 0;
        return result;
    }
    if (API_IS(name,"HeapAlloc"))
        return Win98HeapBlocks::HeapAlloc(e, a[0], a[1], a[2]);
    // The public BFF76Exx wrappers probe p-4 and p+7 under their error-87
    // handler. The span is at most two pages; this also rejects DWORD wrap.
    if (a[2] < 4 || a[2] > ~0u - 7 || !virtual_memory.Access(a[2] - 4, 12, false))
    {
        last_error = 87;
        return 0;
    }
    if (API_IS(name,"HeapFree"))
        return Win98HeapBlocks::HeapFree(e, a[0], a[1], a[2]);
    if (API_IS(name,"HeapSize"))
        return Win98HeapBlocks::HeapSize(e, a[0], a[1], a[2]);
    if (API_IS(name,"HeapReAlloc"))
        return Win98HeapBlocks::HeapReAlloc(e, a[0], a[1], a[2], a[3]);
    Fail("Unsupported heap operation");
    return 0;
}
#include "x86_toolhelp.inc"
#include "x86_kernel_misc.inc"
} // namespace X86
