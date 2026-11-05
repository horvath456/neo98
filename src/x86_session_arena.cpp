// SPDX-License-Identifier: GPL-2.0-or-later
// The session-wide arena. Win98 keeps one area that every process sees at the
// same linear address: the sections with IMAGE_SCN_MEM_SHARED of the loaded
// images, the heaps from HeapCreate with HEAP_SHARED and the views of named
// file mappings.
// A session block is one piece of host memory with one guest address. Every
// process that holds it maps those very host bytes into its own Unicorn
// address space with uc_mem_map_ptr, so a write of one process is a read of
// the other without any reconciliation. Earlier approaches
// copied bytes instead and all failed on the pointers inside the block.
// The table below is static, like the named-object tables in
// src/x86_kernel_objects.cpp: a block belongs to the session, a process only
// holds a reference to it.
#include "x86_app.h"
#include "x86_state_parts.h"
#include "linux_shared_memory.h"
#if defined(__APPLE__) || defined(__linux__)
#include <string.h>
#include <sys/mman.h>
#endif
namespace X86
{
namespace
{
#if defined(__APPLE__) || defined(__linux__)
// On a host with an MMU the host bytes of a block lie in one reserved window
// at the offset of the block's guest address. Two blocks next to each other in
// the guest are then next to each other in the host too, and an engine whose
// host pages are larger than the guest's 4 KiB pages (FEXCore on Apple
// silicon, 16 KiB) can share whole host pages with every
// process: each host page holds exactly the bytes of the guest addresses it
// stands for.
unsigned char *session_window = nullptr;
unsigned char *WindowStorage(unsigned address, unsigned size)
{
    if (!session_window)
    {
        // A saved state knows the window by its name (src/x86_state.cpp).
        void *reserved = MachineState::ReserveWindow("session arena", 1ull << 32);
        if (!reserved) return nullptr;
#if defined(__linux__) && defined(__x86_64__)
        if (ProcessCpuBackend() == CpuBackend::Native && !LinuxSharedMemory::Allocate(1ull << 32, reserved, PROT_NONE)) return nullptr;
#endif
        session_window = static_cast<unsigned char *>(reserved);
    }
    const unsigned long long first = address & ~0xffffull,
                             last = ((unsigned long long)address + size + 0xffff) & ~0xffffull;
    if (mprotect(session_window + first, last - first, PROT_READ | PROT_WRITE)) return nullptr;
    memset(session_window + address, 0, size);
    return session_window + address;
}
#endif
using Win98SessionArena::Granularity;
using Win98SessionArena::InSharedArena;
using Win98SessionArena::KindHeap;
using Win98SessionArena::KindSection;
using Win98SessionArena::KindSystem;
using Win98SessionArena::KindView;
using Win98SessionArena::KindVirtual;
using Win98SessionArena::NameBytes;
using Win98SessionArena::RoundPages;
// These kernel files carry no host string library, so the few operations the
// arena needs are spelled out here.
bool Same(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
void Copy(char *out, const char *text, unsigned capacity)
{
    unsigned i = 0;
    for (; text && text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}
void Append(char *out, const char *text, unsigned capacity)
{
    unsigned i = 0;
    while (i + 1 < capacity && out[i]) ++i;
    for (unsigned j = 0; text && text[j] && i + 1 < capacity; ++j, ++i) out[i] = text[j];
    out[i] = 0;
}
const char *Leaf(const char *path)
{
    const char *leaf = path;
    for (const char *p = path; p && *p; ++p)
        if (*p == '\\' || *p == '/') leaf = p + 1;
    return leaf;
}
void Hex(char *out, unsigned value, unsigned capacity)
{
    char digits[9];
    for (unsigned i = 0; i < 8; ++i) digits[i] = "0123456789abcdef"[(value >> ((7 - i) * 4)) & 15];
    digits[8] = 0;
    Append(out, digits, capacity);
}
bool SameFold(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
    while (*a && lower(*a) == lower(*b)) { ++a; ++b; }
    return lower(*a) == lower(*b);
}
} // namespace
// The gate directory of the session. An API gate is a guest address, and a
// shared section may carry one: COMCTL32 keeps its flat thunk block in its
// `.data` with the trap address of KERNEL32 inside it, and under Win98 that
// address is the same in every process because KERNEL32 lies at one place.
// NEO98 hands out gates in order of first use, so without this directory the
// second process read a trap that meant another import in its own table and
// ended with "Invalid API gate". The directory gives one slot per dll and
// name for the whole session; every process fills that slot in its own table.
Application::ImportTable Application::session_gates;
unsigned Application::session_gate_count = 0;
unsigned Application::SessionGateSlot(const char *dll, const char *name)
{
    if (const unsigned found = session_gates.Find(dll, name, session_gate_count))
        return found - 1;
    // Exit and Callback are return addresses inside the gate region. Their two
    // slots stay without an import in every process.
    while (session_gate_count == (ExitGateSlot) || session_gate_count == (CallbackGateSlot))
    {
        if (!session_gates.Reserve(session_gate_count))
            return ~0u;
        auto &reserved = session_gates[session_gate_count++];
        Copy(reserved.dll, "NEO98", sizeof reserved.dll);
        Copy(reserved.name, "reserved return address", sizeof reserved.name);
        reserved.argc = 0xffffffff;
    }
    if (session_gate_count >= ImportCapacity || !session_gates.Reserve(session_gate_count))
        return ~0u;
    auto &entry = session_gates[session_gate_count];
    Copy(entry.dll, dll, sizeof entry.dll);
    Copy(entry.name, name, sizeof entry.name);
    entry.argc = 0xffffffff;
    session_gates.Index(session_gate_count);
    return session_gate_count++;
}
// The name the session gave a slot, for a process that has not bound it.
const char *Application::SessionGateName(unsigned slot, const char *&dll)
{
    if (slot >= session_gate_count)
        return nullptr;
    dll = session_gates[slot].dll;
    return session_gates[slot].name;
}
// The argument contract of a slot belongs to the session too, so a process
// that meets the gate without having bound it answers the same way.
void Application::PublishSessionGate(unsigned slot, unsigned argc)
{
    if (slot < session_gate_count && argc != 0xffffffff)
        session_gates[slot].argc = argc;
}
// A gate this process never handed out, reached through a shared section. The
// record of the session fills the empty slot.
void Application::AdoptSessionGate(unsigned slot)
{
    if (slot >= session_gate_count)
        return;
    auto &entry = session_gates[slot];
    auto &item = imports[slot];
    Copy(item.dll, entry.dll, sizeof item.dll);
    Copy(item.name, entry.name, sizeof item.name);
    item.argc = entry.argc;
    item.calls = 0;
    item.id = item.route_argument = 0;
    item.route = RouteUnbound;
    imports.Index(slot);
}
// Where a module lies that cannot take its preferred base. Win98 keeps a
// system module at one address for the whole session, so a pointer into it
// means the same thing in every process, and a shared section may carry such
// a pointer: SHELL32 keeps one to OLE32 in its `.data`. NEO98 moves a module
// whose preferred base is occupied, and without this table the
// second process put it somewhere else and followed the first one's pointer
// into nothing.
struct SessionModule
{
    char name[64];
    unsigned base, size;
};
SessionModule session_modules[Application::ModuleCapacity];
unsigned Application::SessionModuleBase(const char *leaf)
{
    for (const auto &entry : session_modules)
        if (entry.base && SameFold(entry.name, leaf))
            return entry.base;
    return 0;
}
// The first process that moves a module decides where it lies, and every later
// process puts it there. A range that would overlap a module the session has
// already placed is not recorded at all: the later process then chooses for
// itself, as it did before, rather than mapping two modules on top of each
// other. Two modules of this tree really do collide that way, because OLE32
// needs C1000h bytes and COMDLG32 used to land 64 KiB above it.
void Application::RecordSessionModuleBase(const char *leaf, unsigned base, unsigned bytes)
{
    if (!base || !bytes || SessionModuleBase(leaf))
        return;
    const unsigned size = (bytes + 0xffffu) & ~0xffffu;
    for (const auto &entry : session_modules)
        if (entry.base && base < entry.base + entry.size && entry.base < base + size)
            return;
    for (auto &entry : session_modules)
        if (!entry.base)
        {
            Copy(entry.name, leaf, sizeof entry.name);
            entry.base = base;
            entry.size = size;
            return;
        }
}
// One block of the session. `storage` is the owned allocation, `host` the
// page-aligned pointer inside it that Unicorn receives.
struct Application::SessionBlock
{
    unsigned base = 0, size = 0, kind = 0, references = 0;
    unsigned virtual_protection = 0;
    unsigned char *storage = nullptr;
    unsigned char *host = nullptr;
    // A heap that HeapDestroy gave back keeps its pages mapped and readable
    // for the rest of the session, and its linear range is never handed out
    // again. MPR reads its provider record after destroying the heap that
    // holds it and survives that under Win98.
    bool retired = false;
    // When it retired; the oldest retired heap is the first to be handed out
    // again (RecycleRetiredSessionHeap).
    unsigned retired_order = 0;
    // Which pages of the block are committed. The state belongs to the
    // session: a process that attaches later gets exactly the pages the
    // others have, because Win98 has one page table for the shared area.
    unsigned char *committed = nullptr;
    char name[NameBytes]{};
    Application *holders[Win98SessionArena::HolderCapacity]{};
    // Whether that holder registered the range itself. A section inside an
    // image the process loaded is already in the page metadata of the image;
    // a section of the shared arena in a process without that image is a
    // range of its own and needs its own entry.
    bool standalone[Win98SessionArena::HolderCapacity]{};
};
Application::SessionBlock Application::session_blocks[96];
Win98SessionArena::Plan Application::session_plan;
unsigned Application::session_retire_clock = 0;
// Every process of the session, in creation order. Win98 keeps the shared area
// mapped in every context, so a heap or a view that the session hands out has
// to be present in every process, not only in the one that asked for it.
Application *Application::session_processes[Win98SessionArena::HolderCapacity];
void Application::JoinSession()
{
    for (auto *&entry : session_processes)
        if (entry == this) return;
        else if (!entry)
        {
            entry = this;
            // Everything the session already holds becomes visible here. A
            // child of EXPLORER reaches MPR's shared heap this way without
            // having asked for it.
            for (auto &block : session_blocks)
                if (block.kind == KindHeap || block.kind == KindView || block.kind == KindVirtual || block.kind == KindSystem ||
                    (block.kind == KindSection && InSharedArena(block.base)))
                    AttachSessionBlock(block);
            return;
        }
}
void Application::LeaveSession()
{
    for (auto *&entry : session_processes)
        if (entry == this) entry = nullptr;
}
// A block whose address the session hands out belongs to every process.
void Application::PublishSessionBlock(SessionBlock &block)
{
    for (auto *process : session_processes)
        if (process && process != this) process->AttachSessionBlock(block);
}
Application::SessionBlock *Application::FindSessionBlock(unsigned address)
{
    for (auto &block : session_blocks)
        if (block.kind && address >= block.base && address - block.base < block.size)
            return &block;
    return nullptr;
}
// A section of the session is found by its address and nothing else. The
// module names the loaders hand in differ in case between processes, and the
// same DLL arrived as both `VERSION.DLL` and `VERSION.dll`; two records for
// one address then mapped over each other and left holes behind.
Application::SessionBlock *Application::FindSessionSection(unsigned address)
{
    for (auto &block : session_blocks)
        if (block.kind == KindSection && block.base == address)
            return &block;
    return nullptr;
}
Application::SessionBlock *Application::FindSessionName(unsigned kind, const char *name)
{
    for (auto &block : session_blocks)
        if (block.kind == kind && Same(block.name, name))
            return &block;
    return nullptr;
}
bool Application::HoldsSessionBlock(const SessionBlock &block) const
{
    for (auto *holder : block.holders)
        if (holder == this) return true;
    return false;
}
Application::SessionBlock *Application::CreateSessionBlock(unsigned kind, unsigned address, unsigned bytes,
                                                           const char *name)
{
    const unsigned size = RoundPages(bytes);
    const unsigned limit = kind == KindVirtual ? 0x3fc00000 : Win98SessionArena::BlockLimit;
    if (!size || size > limit || (address & 4095))
        return nullptr;
    // All records taken: the oldest destroyed shared heap gives its record up.
    bool free_record = false;
    for (const auto &block : session_blocks) free_record |= !block.kind;
    if (!free_record && !RecycleRetiredSessionHeap()) return nullptr;
    for (auto &block : session_blocks)
    {
        if (block.kind) continue;
        // The engine expects host backing at least as large as the mapping.
        // The host bytes lie at the same offset in a 64 KiB granule as the
        // guest address does, with a granule to spare on both sides: a host
        // with larger pages than the guest's 4 KiB (FEXCore on Apple silicon) shares whole host pages, and each of them then
        // maps the right bytes to the right guest addresses.
#if defined(__APPLE__) || defined(__linux__)
        unsigned char *storage = nullptr, *host = WindowStorage(address, size);
        if (!host) return nullptr;
#else
        const unsigned padded = size + 3 * Granularity;
        auto *storage = new unsigned char[padded];
        if (!storage) return nullptr;
        for (unsigned i = 0; i < padded; ++i) storage[i] = 0;
        auto *host = storage + Granularity;
        const unsigned long long value = (unsigned long long)(__SIZE_TYPE__)host;
        host += (unsigned)((address - value) & (Granularity - 1));
#endif
        auto *committed = new unsigned char[size >> 12];
        if (!committed) { delete[] storage; return nullptr; }
        for (unsigned i = 0; i < (size >> 12); ++i) committed[i] = 0;
        block = {};
        block.base = address;
        block.size = size;
        block.kind = kind;
        block.storage = storage;
        block.host = host;
        block.committed = committed;
        Copy(block.name, name, NameBytes);
        return &block;
    }
    return nullptr;
}
// The address backend of one session block. Every page it hands to Unicorn is
// a page of the block's host memory, so the same bytes are in every process.
// Reservation, commit and release keep running through the ordinary metadata
// of VirtualMemory::Space, which is what VirtualQuery answers from.
struct Application::SessionBackend final : VirtualMemory::Backend
{
    Application &app;
    SessionBlock &block;
    // HeapDestroy gives the linear range back, so VirtualQuery has to answer
    // MEM_FREE afterwards. The pages themselves stay where they are: MPR reads
    // its provider record out of the heap it has just destroyed and Win98
    // survives that, because the VMM does not tear down the page table of the
    // shared area on a ring-3 free.
    bool keep_pages = false;
    SessionBackend(Application &a, SessionBlock &b) : app(a), block(b) {}
    bool Available(unsigned base, unsigned size) override { return app.AvailableImageAddress(base, size); }
    bool Map(unsigned base, unsigned size, unsigned permissions) override
    {
        if (base < block.base || base - block.base > block.size - size) return false;
        permissions = app.HostProtection(base, size, permissions & ~(unsigned)Prot::Exec);
        if (app.cpu->MapHost(base, size, permissions, block.host + (base - block.base)) != CpuError::Ok)
            return false;
        app.KeepExecutable(base, size, permissions);
        return true;
    }
    bool Unmap(unsigned base, unsigned size) override
    {
        if (keep_pages) return true;
        app.execute_pages.Remove(base, size);
        return app.cpu->Unmap(base, size) == CpuError::Ok;
    }
    bool Protect(unsigned base, unsigned size, unsigned permissions) override
    {
        permissions = app.HostProtection(base, size, permissions & ~(unsigned)Prot::Exec);
        if (app.cpu->Protect(base, size, permissions) != CpuError::Ok) return false;
        app.KeepExecutable(base, size, permissions);
        return true;
    }
};
// Mapping a block into this process. A section block sits inside an image the
// loader already registered in the page metadata, so only the Unicorn backing
// changes there and the whole section is present at once. A heap or a view
// block is a reservation of its own, and it gets exactly the pages the session
// has committed so far.
bool Application::AttachSessionBlock(SessionBlock &block, bool loaded_section)
{
    if (HoldsSessionBlock(block)) return true;
    // Passive publication must not replace an unrelated image, even when
    // the section has no holder slots left. Only ShareImageSections may
    // replace the private backing of the section it just loaded.
    if (block.kind == KindSection && !loaded_section &&
        virtual_memory.Overlaps(block.base, block.size)) return false;
    unsigned slot = Win98SessionArena::HolderCapacity;
    for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
        if (!block.holders[i]) { slot = i; break; }
    if (slot == Win98SessionArena::HolderCapacity)
    {
        Fail("Session block has no room for another process");
        return false;
    }
    // The guest sees a shared section the way it sees every readable page,
    // Unicorn only what really holds code.
    const unsigned permissions = GuestProtection(Prot::Read | Prot::Write);
    const unsigned host_permissions = HostProtection(block.base, block.size, Prot::Read | Prot::Write);
    if (block.kind == KindSection)
    {
        // Inside an image this process loaded only the backing changes; the
        // page metadata of the image already covers the range. In a process
        // without that image the section is a range of its own.
        const bool inside_image = virtual_memory.Overlaps(block.base, block.size);
        if (inside_image)
            cpu->Unmap(block.base, block.size);
        else if (!virtual_memory.Track(block.base, block.size,
                                       VirtualMemory::Space::Protection(permissions)))
            return false;
        if (cpu->MapHost(block.base, block.size, host_permissions, block.host) != CpuError::Ok)
        {
            if (inside_image)
            {
                cpu->Map(block.base, block.size, host_permissions);
                KeepExecutable(block.base, block.size, host_permissions);
            }
            else
                virtual_memory.Forget(block.base, block.size);
            return false;
        }
        // A section inside an image that holds code keeps the execute right
        // the loader gave its pages; the new backing came without it, and
        // GrantExecute does not grant a page twice.
        KeepExecutable(block.base, block.size, host_permissions);
        block.holders[slot] = this;
        block.standalone[slot] = !inside_image;
        ++block.references;
        return true;
    }
    if (block.kind == KindSystem)
    {
        // Ring-0 memory: readable from ring 3, written only by the native VxD
        // through the host bytes.
        const unsigned read = GuestProtection(Prot::Read);
        if (!virtual_memory.Track(block.base, block.size, VirtualMemory::Space::Protection(read)))
            return false;
        if (cpu->MapHost(block.base, block.size, HostProtection(block.base, block.size, Prot::Read), block.host) !=
            CpuError::Ok)
        {
            virtual_memory.Forget(block.base, block.size);
            return false;
        }
        block.holders[slot] = this;
        block.standalone[slot] = true;
        ++block.references;
        return true;
    }
    SessionBackend backend(*this, block);
    const unsigned reserved = block.kind == KindVirtual
        ? virtual_memory.Reserve(backend, block.base >> 12, block.size >> 12, block.virtual_protection)
        : virtual_memory.ReserveSession(backend, block.base >> 12, block.size >> 12, 0x60000, block.kind == KindView);
    if (reserved != block.base)
        return false;
    block.holders[slot] = this;
    ++block.references;
    const unsigned pages = block.size >> 12;
    for (unsigned i = 0; i < pages;)
    {
        if (!block.committed[i]) { ++i; continue; }
        const unsigned first = i;
        const unsigned protection = block.committed[i];
        while (i < pages && block.committed[i] == protection) ++i;
        if (!CommitSessionPages(block, (block.base >> 12) + first, i - first,
                                block.kind == KindVirtual ? (protection - 1) << 16 : 0x60000))
        {
            ReleaseSessionBlock(block);
            return false;
        }
    }
    return true;
}
// Committing inside a block. The pages belong to the session, so every process
// that holds the block gets them, exactly as one page table would.
unsigned Application::CommitSessionPages(SessionBlock &block, unsigned page, unsigned count, unsigned protection)
{
    if (!count || (page << 12) < block.base || ((page + count) << 12) > block.base + block.size)
        return 0;
    SessionBackend backend(*this, block);
    const unsigned result = block.kind == KindVirtual
                                ? virtual_memory.Commit(backend, page, count, protection)
                                : block.kind == KindView
                                ? virtual_memory.CommitMapping(backend, page, count, 0x60000)
                                : virtual_memory.CommitHeap(backend, page, count);
    if (!result) return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        auto &stored = block.committed[page - (block.base >> 12) + i];
        if (!stored) stored = block.kind == KindVirtual ? (protection >> 16) + 1 : 1;
    }
    for (auto *process : session_processes)
    {
        if (!process || process == this || !process->HoldsSessionBlock(block)) continue;
        SessionBackend peer(*process, block);
        if (block.kind == KindVirtual)
            process->virtual_memory.Commit(peer, page, count, protection);
        else if (block.kind == KindView)
            process->virtual_memory.CommitMapping(peer, page, count, 0x60000);
        else
            process->virtual_memory.CommitHeap(peer, page, count);
    }
    return result;
}
unsigned Application::DecommitSessionPages(SessionBlock &block, unsigned page, unsigned count)
{
    if (!count || (page << 12) < block.base || ((page + count) << 12) > block.base + block.size)
        return 0;
    SessionBackend backend(*this, block);
    const unsigned result = block.kind == KindVirtual ? virtual_memory.Decommit(backend, page, count)
                                                     : virtual_memory.DecommitHeap(backend, page, count);
    if (!result) return 0;
    for (unsigned i = 0; i < count; ++i) block.committed[page - (block.base >> 12) + i] = 0;
    for (auto *process : session_processes)
    {
        if (!process || process == this || !process->HoldsSessionBlock(block)) continue;
        SessionBackend peer(*process, block);
        if (block.kind == KindVirtual) process->virtual_memory.Decommit(peer, page, count);
        else process->virtual_memory.DecommitHeap(peer, page, count);
    }
    // Fresh backing is zero backing. The host bytes outlive the mapping, so a
    // decommit clears them; otherwise the next commit would hand the old
    // contents back, and the original heap relies on a committed page being
    // zero. A process that only gives up its own reference does not come here.
    const unsigned start = (page << 12) - block.base;
    for (unsigned i = 0; i < (count << 12); ++i) block.host[start + i] = 0;
    return result;
}
void Application::ReleaseSessionBlock(SessionBlock &block)
{
    unsigned slot = Win98SessionArena::HolderCapacity;
    for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
        if (block.holders[i] == this) { slot = i; break; }
    if (slot == Win98SessionArena::HolderCapacity) return;
    const bool standalone = block.standalone[slot];
    block.holders[slot] = nullptr;
    block.standalone[slot] = false;
    if (block.references) --block.references;
    if (cpu)
    {
        if (block.kind == KindSection || block.kind == KindSystem)
        {
            cpu->Unmap(block.base, block.size);
            if (standalone) virtual_memory.Forget(block.base, block.size);
        }
        else if (block.retired)
        {
            // The metadata went with the retire; what is left are the pages
            // the heap had committed. The engine unmaps only mapped ranges, so
            // they go run by run.
            const unsigned pages = block.size >> 12;
            for (unsigned i = 0; i < pages;)
            {
                if (!block.committed[i]) { ++i; continue; }
                const unsigned first = i;
                while (i < pages && block.committed[i]) ++i;
                execute_pages.Remove(block.base + (first << 12), (i - first) << 12);
                cpu->Unmap(block.base + (first << 12), (i - first) << 12);
            }
        }
        else
        {
            SessionBackend backend(*this, block);
            if (block.kind == KindVirtual)
                virtual_memory.Release(backend, block.base);
            else if (block.kind == KindView)
                virtual_memory.ReleaseMapping(backend, block.base);
            else
                virtual_memory.ReleaseHeap(backend, block.base);
        }
    }
    if (block.references) return;
    delete[] block.storage;
    delete[] block.committed;
    block = {};
}
// The loader is about to unmap a whole image. Every session section inside it
// gives its backing back first, and the hole gets ordinary pages again so the
// caller can unmap one contiguous range.
void Application::DropSessionBlocksIn(unsigned base, unsigned size)
{
    for (auto &block : session_blocks)
    {
        if (block.kind != KindSection || block.base < base || block.base - base >= size) continue;
        if (!HoldsSessionBlock(block)) continue;
        const unsigned at = block.base, bytes = block.size;
        const unsigned permissions = HostProtection(at, bytes, Prot::Read | Prot::Write);
        bool standalone = false;
        for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
            if (block.holders[i] == this) standalone = block.standalone[i];
        ReleaseSessionBlock(block);
        // The image metadata still covers this range, so the hole gets
        // ordinary pages again and the caller can unmap one whole image.
        if (!standalone)
        {
            cpu->Map(at, bytes, permissions);
            KeepExecutable(at, bytes, permissions);
        }
        // KERNEL32 counts module owners separately from mappings:
        // _PageFree follows the last MTE reference. A passive process sees
        // the shared arena but does not keep an unloaded DLL's data alive.
        // Otherwise its next load inherits stale helper PIDs and pointers. Keep the bytes while any process still owns the section.
        bool loaded = false;
        for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
            loaded |= block.holders[i] && !block.standalone[i];
        if (!loaded)
            for (auto *holder : block.holders)
                if (holder) holder->ReleaseSessionBlock(block);
    }
}
// A module is about to be mapped over a range where this process only holds
// session sections of other images. They go back first, so the loader sees the
// address as free and can map the image in one piece; ShareImageSections puts
// the ones that belong to this image back afterwards.
void Application::ReleaseSessionSectionsIn(unsigned base, unsigned size)
{
    for (auto &block : session_blocks)
    {
        if (block.kind != KindSection) continue;
        if (block.base + block.size <= base || base + size <= block.base) continue;
        bool standalone = false;
        for (unsigned i = 0; i < Win98SessionArena::HolderCapacity; ++i)
            if (block.holders[i] == this) standalone = block.standalone[i];
        if (standalone) ReleaseSessionBlock(block);
    }
}
// A process that ends gives up every block it holds. The Unicorn address space
// goes away with it, so nothing is unmapped here; what matters is the count
// that keeps the host bytes alive for the other processes.
void Application::FreeSessionBlocks()
{
    if (process_database) RetireSessionHeap(process_database);
    LeaveSession();
    for (auto &block : session_blocks)
    {
        if (!block.kind || !HoldsSessionBlock(block)) continue;
        for (auto *&holder : block.holders)
            if (holder == this) holder = nullptr;
        if (block.references) --block.references;
        if (block.references) continue;
        if (block.kind == KindHeap) ForgetSessionNls(block.base);
        delete[] block.storage;
        delete[] block.committed;
        block = {};
    }
}
// Win98's TDB constructor puts the owning PDB in TIB+30h.
// Windows 98 allocates a C4h-byte kernel object of type 6 on the shared
// kernel heap. In particular this is a shared-arena pointer, not an NT
// PEB. Packers use its sign to select their Win9x import-resolution path.
// Native process services keep their remaining state in Application; this
// guest record publishes the identity and object type, not an NT loader list.
bool Application::InitializeProcessDatabase()
{
    if (process_database) return true;
    const unsigned address = ReserveSessionHeap(1);
    if (address == ~0u) return false;
    if (!CommitSessionHeap(address >> 12, 1))
    {
        RetireSessionHeap(address);
        return false;
    }
    process_database = address;
    Write32(address, 6);
    return state != Failed;
}
// Every section with IMAGE_SCN_MEM_SHARED of a freshly loaded image. The first
// process puts the bytes it just loaded into the session, every later process
// takes what is already there, exactly as the page tables of Win98 do.
void Application::ShareImageSections(unsigned module)
{
    if (module >= ModuleCapacity) return;
    auto &m = modules[module];
    if (m.kind != 3 || !m.pe) return;
    Pe32::Image::SharedSection sections[16];
    const unsigned count = m.pe->SharedSections(sections, 16);
    if (!count) return;
    const unsigned base = m.pe->Base();
    const bool preferred = base == m.pe->PreferredBase();
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned address = base + sections[i].rva, size = RoundPages(sections[i].size);
        char name[NameBytes];
        Copy(name, Leaf(m.path), NameBytes);
        Append(name, "!", NameBytes);
        Append(name, sections[i].name, NameBytes);
        if (!preferred)
        {
            // Win98 never moves a system module that carries shared sections.
            // NEO98 may, and then the section cannot be shared, because the
            // guest addresses differ between the processes. Say so.
            if (log)
            {
                char detail[240];
                Copy(detail, "SESSION section ", sizeof detail);
                Append(detail, name, sizeof detail);
                Append(detail, " stays private: base ", sizeof detail);
                Hex(detail, base, sizeof detail);
                Append(detail, " is not the preferred base ", sizeof detail);
                Hex(detail, m.pe->PreferredBase(), sizeof detail);
                log(detail);
            }
            continue;
        }
        auto *block = FindSessionSection(address);
        if (block && (block->size != size || !SameFold(block->name, name)))
        {
            if (log)
            {
                char detail[240];
                Copy(detail, "SESSION section ", sizeof detail);
                Append(detail, name, sizeof detail);
                Append(detail, " stays private: ", sizeof detail);
                Append(detail, block->name, sizeof detail);
                Append(detail, " already holds ", sizeof detail);
                Hex(detail, address, sizeof detail);
                log(detail);
            }
            continue;
        }
        const bool fresh = !block;
        if (!block)
        {
            block = CreateSessionBlock(KindSection, address, size, name);
            if (!block) continue;
            // The bytes this process just loaded and bound become the bytes of
            // the session.
            if (cpu->Read(address, block->host, size) != CpuError::Ok)
            {
                delete[] block->storage;
                delete[] block->committed;
                *block = {};
                continue;
            }
        }
        if (AttachSessionBlock(*block, true))
        {
            // A section of the shared arena belongs to every process of the
            // session, whether or not that process loaded the module. Win98
            // has 80000000h to in every context, and DDHELP.EXE
            // reads a string out of the shared `.data` of DSOUND without
            // having DSOUND loaded.
            if (InSharedArena(address)) PublishSessionBlock(*block);
        }
        else if (fresh)
        {
            delete[] block->storage;
            delete[] block->committed;
            *block = {};
        }
    }
}
// HeapCreate with HEAP_SHARED reserves in the VMM arena 80060000h. The session
// hands out that address, so the heap has the same base in every process.
unsigned Application::ReserveSessionHeap(unsigned pages)
{
    if (!pages || pages > (Win98SessionArena::BlockLimit >> 12)) return ~0u;
    const unsigned bytes = pages << 12;
    const unsigned base = ReserveSessionRange(bytes);
    if (!base) return ~0u;
    const unsigned size = (bytes + (Granularity - 1)) & ~(Granularity - 1);
    auto *block = CreateSessionBlock(KindHeap, base, size, "heap");
    if (!block) return ~0u;
    if (!AttachSessionBlock(*block))
    {
        delete[] block->storage;
        delete[] block->committed;
        *block = {};
        return ~0u;
    }
    PublishSessionBlock(*block);
    return base;
}
// A second process reaches a shared heap through the handle another process
// left in a shared section. Its own heap table learns the heap here.
unsigned Application::AdoptSessionHeap(unsigned handle)
{
    auto *block = FindSessionBlock(handle);
    if (!block || block->kind != KindHeap || block->base != handle || block->retired) return 0;
    if (!AttachSessionBlock(*block)) return 0;
    return handle;
}
bool Application::SessionHeapAddress(unsigned address) const
{
    auto *block = FindSessionBlock(address);
    return block && block->kind == KindHeap;
}
// HeapDestroy gives the heap back. The pages stay mapped and readable for the
// rest of the session, and the linear range is never handed out again, but the
// handle is no heap any more, here and in every other process.
unsigned Application::CommitSessionHeap(unsigned page, unsigned count)
{
    auto *block = FindSessionBlock(page << 12);
    return block && block->kind == KindHeap ? CommitSessionPages(*block, page, count) : 0;
}
unsigned Application::DecommitSessionHeap(unsigned page, unsigned count)
{
    auto *block = FindSessionBlock(page << 12);
    return block && block->kind == KindHeap ? DecommitSessionPages(*block, page, count) : 0;
}
unsigned Application::CommitSessionView(unsigned page, unsigned count)
{
    auto *block = FindSessionBlock(page << 12);
    return block && block->kind == KindView ? CommitSessionPages(*block, page, count) : 0;
}
void Application::RetireSessionHeap(unsigned base)
{
    auto *block = FindSessionBlock(base);
    if (!block || block->kind != KindHeap || block->base != base) return;
    block->retired = true;
    block->retired_order = ++session_retire_clock;
    // The reservation goes back in every process that holds the block, so
    // VirtualQuery answers MEM_FREE, and the pages stay mapped.
    for (auto *process : session_processes)
    {
        if (!process || !process->HoldsSessionBlock(*block) || !process->cpu) continue;
        SessionBackend backend(*process, *block);
        backend.keep_pages = true;
        process->virtual_memory.ReleaseHeap(backend, base);
    }
}
// A range of the session window. The window is first handed out from the
// bottom up, so a range freed early is not given to someone else at once
// (MPR reads its destroyed heap again). Once the top is reached the
// first gap between the blocks of the session is taken, the way _PageReserve
// of Win98 hands out a range that _PageFree gave back, and when there is none
// the oldest destroyed shared heap goes. RPCRT4 makes two shared heaps and a
// view on every load, and an MSI install loads it with each DLL custom action

unsigned Application::ReserveSessionRange(unsigned bytes)
{
    if (const unsigned base = session_plan.Reserve(bytes)) return base;
    if (!bytes || bytes > Win98SessionArena::BlockLimit) return 0;
    const unsigned size = (bytes + (Granularity - 1)) & ~(Granularity - 1);
    for (;;)
    {
        unsigned candidate = Win98SessionArena::Base;
        for (bool moved = true; moved;)
        {
            moved = false;
            for (const auto &block : session_blocks)
            {
                if (!block.kind || block.base + block.size <= candidate || candidate + size <= block.base) continue;
                candidate = (block.base + block.size + (Granularity - 1)) & ~(Granularity - 1);
                moved = true;
            }
            if (candidate < Win98SessionArena::Base || size > Win98SessionArena::Limit - candidate) break;
        }
        if (candidate >= Win98SessionArena::Base && size <= Win98SessionArena::Limit - candidate) return candidate;
        if (!RecycleRetiredSessionHeap()) return 0;
    }
}
// The oldest heap HeapDestroy gave back leaves the session: every process that
// still has its pages mapped lets them go, and its record and its range are
// free again.
bool Application::RecycleRetiredSessionHeap()
{
    SessionBlock *oldest = nullptr;
    for (auto &block : session_blocks)
        if (block.kind == KindHeap && block.retired && (!oldest || block.retired_order < oldest->retired_order))
            oldest = &block;
    if (!oldest) return false;
    ForgetSessionNls(oldest->base);
    for (auto *holder : oldest->holders)
        if (holder && oldest->kind) holder->ReleaseSessionBlock(*oldest);
    if (oldest->kind)
    {
        delete[] oldest->storage;
        delete[] oldest->committed;
        *oldest = {};
    }
    return true;
}
// The view of a named file mapping. Every process gets the same address, so a
// pointer stored inside the block means the same thing on both sides.
unsigned Application::ReserveSessionView(const char *name, unsigned bytes)
{
    if (!name || !*name || !bytes) return 0;
    auto *block = FindSessionName(KindView, name);
    if (block)
    {
        if (block->size < RoundPages(bytes)) return 0;
        return AttachSessionBlock(*block) ? block->base : 0;
    }
    const unsigned base = ReserveSessionRange(bytes);
    if (!base) return 0;
    const unsigned size = (RoundPages(bytes) + (Granularity - 1)) & ~(Granularity - 1);
    block = CreateSessionBlock(KindView, base, size, name);
    if (!block) return 0;
    if (!AttachSessionBlock(*block))
    {
        delete[] block->storage;
        delete[] block->committed;
        *block = {};
        return 0;
    }
    PublishSessionBlock(*block);
    return base;
}
// MEM_SHARED is an allocation in the Win98 shared arena, not merely an
// address hint. WINSPOOL passes these buffers directly to SPOOL32.
bool Application::IsSharedVirtual(unsigned address)
{
    const auto *block = FindSessionBlock(address);
    return block && block->kind == KindVirtual;
}
unsigned Application::ReserveSharedVirtual(unsigned page, unsigned pages, unsigned flags)
{
    if (!pages || pages > 0x3fc00 || (flags & ~0x60000u)) return ~0u;
    const unsigned bytes = pages << 12;
    unsigned base = page == 0x80060000 ? ReserveSessionRange(bytes) : page << 12;
    // Large virtual reservations can exceed the heap/view arena's 64 MiB
    // block limit. Retain the VMM's upper shared range, leaving GDI's DIB
    // window out, and choose an address free in every process.
    if (!base && page == 0x80060000)
        for (unsigned candidate = 0x90000000; candidate < 0xbfc00000 && bytes <= 0xbfc00000 - candidate;
             candidate += Granularity)
        {
            if (candidate < 0xb0000000 && candidate + bytes > 0xa0000000)
            { candidate = 0xb0000000 - Granularity; continue; }
            bool available = true;
            for (const auto &other : session_blocks)
                if (other.kind && candidate < other.base + other.size && other.base < candidate + bytes)
                    available = false;
            for (auto *process : session_processes)
                if (process && !process->AvailableImageAddress(candidate, bytes)) available = false;
            if (available) { base = candidate; break; }
        }
    if (base < 0x80000000 || base >= 0xbfc00000 || bytes > 0xbfc00000 - base) return ~0u;
    for (const auto &other : session_blocks)
        if (other.kind && base < other.base + other.size && other.base < base + bytes) return ~0u;
    for (auto *process : session_processes)
        if (process && !process->AvailableImageAddress(base, bytes)) return ~0u;
    auto *block = CreateSessionBlock(KindVirtual, base, bytes, "VirtualAlloc MEM_SHARED");
    if (!block) return ~0u;
    block->virtual_protection = flags;
    if (!AttachSessionBlock(*block))
    {
        delete[] block->storage;
        delete[] block->committed;
        *block = {};
        return ~0u;
    }
    if (base >= session_plan.next && base < Win98SessionArena::Limit)
        session_plan.next = (base + bytes + Granularity - 1) & ~(Granularity - 1);
    PublishSessionBlock(*block);
    return base;
}
unsigned Application::CommitSharedVirtual(unsigned page, unsigned pages, unsigned flags)
{
    auto *block = FindSessionBlock(page << 12);
    return block && block->kind == KindVirtual && AttachSessionBlock(*block)
        ? CommitSessionPages(*block, page, pages, flags) : 0;
}
unsigned Application::DecommitSharedVirtual(unsigned page, unsigned pages)
{
    auto *block = FindSessionBlock(page << 12);
    return block && block->kind == KindVirtual && HoldsSessionBlock(*block)
        ? DecommitSessionPages(*block, page, pages) : 0;
}
unsigned Application::ReleaseSharedVirtual(unsigned base)
{
    auto *block = FindSessionBlock(base);
    if (!block || block->kind != KindVirtual || block->base != base || !HoldsSessionBlock(*block)) return 0;
    for (auto *holder : block->holders)
        if (holder) holder->ReleaseSessionBlock(*block);
    return 1;
}
unsigned Application::ProtectSharedVirtual(unsigned address, unsigned bytes, unsigned flags)
{
    auto *block = FindSessionBlock(address);
    if (!block || block->kind != KindVirtual || !HoldsSessionBlock(*block)) return ~0u;
    SessionBackend backend(*this, *block);
    const unsigned previous = virtual_memory.Protect(backend, address, bytes, flags);
    if (previous == ~0u) return ~0u;
    const unsigned first = (address - block->base) >> 12;
    const unsigned count = ((address & 4095) + (bytes ? bytes : 1) + 4095) >> 12;
    for (unsigned i = 0; i < count; ++i) block->committed[first + i] = (flags >> 16) + 1;
    for (auto *holder : block->holders)
        if (holder && holder != this)
        {
            SessionBackend peer(*holder, *block);
            holder->virtual_memory.Protect(peer, address, bytes, flags);
        }
    return previous;
}
// The host bytes behind a stretch of a session block, the same bytes every
// process of the session maps at that address.
unsigned char *Application::SessionBytes(unsigned address, unsigned bytes)
{
    auto *block = FindSessionBlock(address);
    if (!block || !block->host || bytes > block->size - (address - block->base)) return nullptr;
    return block->host + (address - block->base);
}
// The memory of a native VxD at a fixed ring-0 address, created once for the
// session and mapped into every process that exists now or starts later. The
// host writes through the returned bytes; the guest only reads.
unsigned char *Application::SystemArena(const char *name, unsigned address, unsigned bytes)
{
    SessionBlock *block = FindSessionName(KindSystem, name);
    if (!block)
    {
        block = CreateSessionBlock(KindSystem, address, bytes, name);
        if (!block) return nullptr;
        for (unsigned i = 0; i < (block->size >> 12); ++i) block->committed[i] = 1;
    }
    JoinSession();
    if (!AttachSessionBlock(*block)) return nullptr;
    PublishSessionBlock(*block);
    return block->host;
}
} // namespace X86
