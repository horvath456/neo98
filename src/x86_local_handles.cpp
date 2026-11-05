// SPDX-License-Identifier: GPL-2.0-or-later
// Native connection for the implemented KERNEL32 LocalSize and
// ConvertToGlobalHandle. The decisions live in win98_local_handles.h.
#include "win98_local_handles.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
unsigned Application::LocalHandleApi(ApiName name, const unsigned *a)
{
    if (API_IS(name,"LocalSize"))
    {
        struct SizeEnvironment final : Win98LocalHandles::SizeEnvironment
        {
            Application &app;
            explicit SizeEnvironment(Application &value) : app(value) {}
            bool Readable(unsigned handle) override
            {
                // The original probes [handle+7] and [handle-4] behind its own
                // exception frame. The native memory manager answers the same
                // question without raising.
                unsigned char byte = 0;
                return handle >= 4 && app.Read(handle + 7, &byte, 1) && app.Read(handle - 4, &byte, 1);
            }
            bool Entry(unsigned handle, unsigned &signature, unsigned &block) override
            {
                unsigned short word = 0;
                if (!app.Read(handle, &word, 2))
                    return false;
                signature = word;
                block = app.Read32(handle + 2);
                return true;
            }
            bool BlockSize(unsigned pointer, unsigned &size) override
            {
                size = app.AllocationSize(pointer, true);
                return size != 0;
            }
            void Error(unsigned code) override { app.last_error = code; }
        } environment(*this);
        return Win98LocalHandles::Size(environment, a[0]);
    }
    struct GlobalEnvironment final : Win98LocalHandles::GlobalEnvironment
    {
        Application &app;
        explicit GlobalEnvironment(Application &value) : app(value) {}
        // The shared process of the original is the session's table of
        // global handles: Share duplicates the object into it and closes the
        // source (Application::PublishGlobalHandle), and the mixing moves the
        // result out of the 6xxxxxxxh space every process numbers its own
        // entries in.
        bool SharedProcess() override { return false; }
        unsigned Share(unsigned handle) override { return app.PublishGlobalHandle(handle); }
        bool Mix() override { return true; }
    } environment(*this);
    const unsigned global = a[0] ? Win98LocalHandles::ToGlobal(environment, a[0]) : 0;
    if (!global)
    {
        // The original reaches zero through its failed open or duplicate: an
        // object it cannot duplicate into the shared process has no global
        // handle. A thread handle is such an object, because its record
        // belongs to the process that owns the thread.
        last_error = Win98LocalHandles::InvalidHandle;
        return 0;
    }
    return global;
}
unsigned Application::ReallocateFixed(const unsigned *a)
{
        unsigned size = AllocationSize(a[0]);
        if (!size)
            return 0;
        if ((a[2] & 0x80) || a[1] <= size)
            return a[0]; // A fixed block must retain its address when shrinking.
        // Extend fixed memory in place where the arena permits it.
        if (a[1] > HeapArenaLimit - 15)
        {
            last_error = 8;
            return 0;
        }
        const unsigned wanted = (a[1] + 15) & ~15u;
        unsigned result = 0;
        for (auto &block : AllocationBlocks())
            if (block.used && block.address == a[0])
            {
                unsigned end = block.address + block.size;
                if (end == next_alloc && wanted - size > heap_end - next_alloc)
                    GrowHeap(wanted - size, false);
                if (end == next_alloc && wanted - size <= heap_end - next_alloc)
                {
                    next_alloc += wanted - size;
                    block.size = wanted;
                    result = a[0];
                }
                else
                    for (auto &free : AllocationBlocks())
                        if (!free.used && free.address == end && free.size >= wanted - size)
                        {
                            unsigned extra = wanted - size;
                            free.address += extra;
                            free.size -= extra;
                            if (!free.size)
                                free = {};
                            block.size = wanted;
                            result = a[0];
                            break;
                        }
                break;
            }
        if (!result)
        {
            if (!(a[2] & 2))
            {
                last_error = 8;
                return 0;
            }
            result = Allocate(a[1]);
        }
        if (result)
        {
            unsigned char buffer[1024];
            unsigned count = a[1] < size ? a[1] : size;
            for (unsigned i = 0; i < count; i += sizeof buffer)
            {
                unsigned n = count - i;
                if (n > sizeof buffer)
                    n = sizeof buffer;
                Read(a[0] + i, buffer, n);
                Store(result + i, buffer, n);
            }
            if (a[2] & 64)
            {
                memset(buffer, 0, sizeof buffer);
                for (unsigned i = count; i < AllocationSize(result); i += sizeof buffer)
                {
                    unsigned n = AllocationSize(result) - i;
                    if (n > sizeof buffer)
                        n = sizeof buffer;
                    Store(result + i, buffer, n);
                }
            }
            if (result != a[0])
                Free(a[0]);
        }
        return result;
}
// Public Local/Global memory: moveable handles retain their identity when the
// data block moves or is discarded. The handle tag/signature follows KERNEL32;
// allocation and lock metadata belong to the process and its saved state.
unsigned Application::LocalMemoryApi(ApiName name, const unsigned *a)
{
    const bool alloc = API_IS(name,"LocalAlloc") || API_IS(name,"GlobalAlloc");
    auto zero = [&](unsigned address, unsigned size) {
        const unsigned char bytes[1024]{};
        while (size) {
            const unsigned n = size < sizeof bytes ? size : sizeof bytes;
            Store(address, bytes, n); address += n; size -= n;
        }
    };
    auto capacity = [&](unsigned address, unsigned requested, bool moveable, bool grow) {
        // Win98 Local/Global blocks expose DWORD-sized capacities, with a
        // minimum of 12 bytes fixed and 8 bytes moveable. The native arena's
        // 16-byte storage alignment is not the size of the public block.
        unsigned size = (requested + 3u) & ~3u;
        const unsigned minimum = moveable ? 8u : 12u;
        if (size < minimum) size = minimum;
        for (auto &entry : AllocationBlocks()) if (entry.used && entry.address == address) {
            if (!grow || size > entry.local_size) entry.local_size = size;
            break;
        }
    };
    if (alloc) {
        const bool moveable = (a[0] & 2) != 0;
        // KERNEL32 passes fixed Local/Global allocations
        // to on PDB+18h. Keep its free lists and uninitialized reuse,
        // rather than sharing the host's temporary guest-buffer arena.
        if (!moveable) return ProcessHeapBlock(a[1], a[0] & 0x40);
        const unsigned data = a[1] ? Allocate(a[1]) : 0;
        if (a[1] && !data) return 0;
        if (data) capacity(data, a[1], true, false);
        if (data && (a[0] & 0x40)) zero(data, AllocationSize(data));
        const unsigned entry = Allocate(16);
        if (!entry) { if (data) Free(data); return 0; }
        for (auto &b : AllocationBlocks()) if (b.used && b.address == entry) {
            b.moveable = true; b.data = data; b.locks = 0; b.flags = a[0];
            zero(entry, 16);
            const unsigned short signature = 0x5342;
            Store(entry, &signature, 2); Write32(entry + 2, data);
            return entry + 2;
        }
        return 0;
    }
    const bool free = API_IS(name,"LocalFree") || API_IS(name,"GlobalFree");
    if (free && !a[0]) return 0;
    Block *block = nullptr;
    for (auto &b : AllocationBlocks()) if (b.used &&
            a[0] == b.address + (b.moveable ? 2u : 0u)) { block = &b; break; }
    if (!block && heap_system) {
        // Fixed Local/Global handles may be ordinary process-heap pointers.
        // SNMPAPI passes a HeapAlloc buffer to GlobalReAlloc.
        const unsigned kept=last_error,none[]={0};
        const unsigned heap=HeapApi("GetProcessHeap",none),query[]={heap,0,a[0]};
        const unsigned size=HeapApi("HeapSize",query);
        last_error=kept;
        if(size){
            if(API_IS(name,"LocalSize")||API_IS(name,"GlobalSize"))return size;
            if(API_IS(name,"LocalFlags")||API_IS(name,"GlobalFlags"))return 0;
            if(API_IS(name,"LocalLock")||API_IS(name,"GlobalLock")||API_IS(name,"GlobalWire")||
               API_IS(name,"LocalHandle")||API_IS(name,"GlobalHandle"))return a[0];
            if(API_IS(name,"GlobalUnlock"))return 1;
            if(API_IS(name,"LocalUnlock")||API_IS(name,"GlobalUnWire")||
               API_IS(name,"GlobalFix")||API_IS(name,"GlobalUnfix"))return 0;
            if(free)return HeapApi("HeapFree",query)?0:a[0];
            // Windows 98 rejects GMEM_MODIFY for a fixed pointer,
            // including MODIFY|MOVEABLE; it does not convert the block.
            if(a[2]&0x80){last_error=87;return 0;}
            const unsigned resize[]={heap,((a[2]&0x40)?8u:0u)|((a[2]&2)?0u:0x10u),a[0],a[1]};
            return HeapApi("HeapReAlloc",resize);
        }
    }
    if (!block) {
        last_error = 6;
        if (free) return a[0];
        return API_IS(name,"LocalFlags") || API_IS(name,"GlobalFlags") ? 0x8000 : 0;
    }
    auto &b = *block;
    const unsigned data = b.moveable ? b.data : b.address;
    if (API_IS(name,"LocalSize") || API_IS(name,"GlobalSize")) return AllocationSize(data, true);
    if (API_IS(name,"LocalFlags") || API_IS(name,"GlobalFlags"))
        return b.moveable ? b.locks | (data ? 0 : 0x4000) | (b.flags & 0x100) : 0;
    if (API_IS(name,"LocalLock") || API_IS(name,"GlobalLock") || API_IS(name,"GlobalWire")) {
        if (b.moveable && data && b.locks < 255) ++b.locks;
        return data;
    }
    if (API_IS(name,"LocalUnlock") || API_IS(name,"GlobalUnlock") || API_IS(name,"GlobalUnWire")) {
        if (!b.moveable) return API_IS(name,"GlobalUnlock") ? 1 : 0;
        if (b.locks) --b.locks;
        return b.locks != 0;
    }
    if (free) {
        const unsigned entry = b.address;
        if (b.moveable && data) Free(data);
        Free(entry); return 0;
    }
    if (API_IS(name,"LocalHandle") || API_IS(name,"GlobalHandle")) return a[0];
    if (API_IS(name,"GlobalFix")) { if (b.moveable && data && b.locks < 255) ++b.locks; return 0; }
    if (API_IS(name,"GlobalUnfix")) { if (b.moveable && b.locks) --b.locks; return 0; }
    if ((a[2] & 0x82) == 0x82 && !b.moveable) {
        const unsigned create[] = {2, 0};
        const unsigned handle = LocalMemoryApi("GlobalAlloc", create);
        if (!handle) return 0;
        for (auto &entry : AllocationBlocks()) if(entry.used && entry.moveable && entry.address+2==handle) {
            entry.data=data;entry.flags=a[2];Write32(handle,data);break;
        }
        return handle;
    }
    if (a[2] & 0x80) { b.flags = (b.flags & ~0x100u) | (a[2] & 0x100); return a[0]; }
    if (!b.moveable) {
        const unsigned result = ReallocateFixed(a);
        if (result) capacity(result, a[1], false, true);
        return result;
    }
    if (!a[1]) {
        if (b.locks) { last_error = 158; return 0; }
        if (data) Free(data);
        b.data = 0; Write32(b.address + 2, 0); return a[0];
    }
    unsigned replacement = 0;
    if (data) {
        // A locked block moves only when the caller passes GMEM_MOVEABLE;
        // otherwise it must grow in place or fail with ERROR_LOCKED
        // (measured on Win98 SE, global_realloc contract).
        const bool may_move = (a[2] & 2) || !b.locks;
        const unsigned args[] = {data, a[1], (a[2] & ~2u) | (may_move ? 2u : 0u)};
        replacement = ReallocateFixed(args);
        if (!replacement && !may_move) last_error = 212;
    } else {
        replacement = Allocate(a[1]);
        if (replacement && (a[2] & 0x40)) zero(replacement, AllocationSize(replacement));
    }
    if (!replacement) return 0;
    capacity(replacement, a[1], true, true);
    b.data = replacement; Write32(b.address + 2, replacement); return a[0];
}
} // namespace X86
