// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for KERNEL32 ordinal exports 1–60: VxD calls, strings,
// unsupported exports, heap services, and Win16 bridges. Helpers in
// win98_kernel_ordinals_a.h define behavior; this file supplies guest memory,
// heaps, NLS, threads, and unsupported-transition errors.
#include "win98_kernel_ordinals_a.h"
#include "win98_segment_map.h"
#include "win98_win16_module.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace K = Win98KernelOrdinals;

// The two 128-byte translation tables of the original's.data are built the
// way builds them: bytes 0x80..0xff go through the native
// MultiByteToWideChar and WideCharToMultiByte between CP_ACP (0) and CP_OEMCP
// (1), flags 0, no default character. The original ignores the conversion
// results; NEO98 stops when the NLS provider cannot answer.
bool Application::BuildOemTables()
{
    if (oem_tables_ready) return true;
    const unsigned alloc[] = {0x40, 0x300};
    const unsigned scratch = Api("GlobalAlloc", alloc);
    if (!scratch) { Fail("OEM table scratch allocation failed"); return false; }
    const unsigned bytes = scratch, wide = scratch + 0x100, out = scratch + 0x200, used = scratch + 0x2f0;
    unsigned char identity[128];
    for (unsigned i = 0; i < 128; ++i) identity[i] = (unsigned char)(0x80 + i);
    bool ok = Store(bytes, identity, sizeof identity);
    for (unsigned direction = 0; ok && direction < 2; ++direction)
    {
        const unsigned from = direction ? 0 : 1, to = direction ? 1 : 0; // OEM->ANSI first, as
        const unsigned widen[] = {from, 0, bytes, 128, wide, 128};
        const unsigned narrow[] = {to, 0, wide, 128, out, 128, 0, used};
        ok = Api("MultiByteToWideChar", widen) == 128 && Api("WideCharToMultiByte", narrow) == 128 &&
             Read(out, direction ? ansi_high : oem_high, 128);
    }
    const unsigned release[] = {scratch};
    Api("GlobalFree", release);
    if (!ok) { Fail("OEM translation tables need the SBCS NLS conversions"); return false; }
    oem_tables_ready = true;
    return true;
}

// Ordinals 10..13. Null pointers answer zero without an error code; a probe
// failure sets 87. The original probes with SEH-guarded touches;
// NEO98 asks its memory manager.
unsigned Application::OemStringApi(unsigned ordinal, const unsigned *a)
{
    const bool buffered = ordinal == 11 || ordinal == 13, to_oem = ordinal <= 11;
    const unsigned source = a[0], target = a[1], count = buffered ? a[2] : 0;
    if (!source || !target) return 0;
    if (!BuildOemTables()) return 0;
    unsigned length = count;
    if (!buffered)
    {
        length = 0;
        for (;;)
        {
            if (!virtual_memory.Access(source + length, 1, false)) { last_error = K::InvalidParameter; return 0; }
            unsigned char c = 0;
            Read(source + length, &c, 1);
            if (!c) break;
            ++length;
        }
        ++length; // The terminator is written too.
    }
    if (length && (!virtual_memory.Access(source, length, false) || !virtual_memory.Access(target, length, true)))
    {
        last_error = K::InvalidParameter;
        return 0;
    }
    const unsigned char *high = to_oem ? ansi_high : oem_high;
    auto read = [&](unsigned i) { unsigned char c = 0; Read(source + i, &c, 1); return c; };
    auto write = [&](unsigned i, unsigned char c) { Store(target + i, &c, 1); };
    if (buffered) K::ConvertBuffer(high, to_oem, count, read, write);
    else K::ConvertString(high, to_oem, read, write);
    return 1;
}

// Ordinal 17. Register entry: ECX carries the encoding, the stub returns to
// the caller of the export that jumped here.
unsigned Application::UnimpStubApi()
{
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp), ecx = Register(Reg::Ecx);
    last_error = K::UnimpStubError;
    Register(Reg::Eax, K::UnimpStubResult(ecx));
    Register(Reg::Esp, sp + 4 + K::UnimpStubPop(ecx));
    Register(Reg::Eip, ret);
    return 0;
}

// Win16 global heap bridge. Blocks come from the native GlobalAlloc; the
// handle table keeps the Win16 handle shape and the lock counts.
unsigned Application::Win16GlobalApi(unsigned ordinal, const unsigned *a)
{
    auto find = [&](unsigned handle) -> Win16Global * {
        unsigned index;
        if (!K::Win16HandleIndex(handle, index) || index >= 64 || !win16_globals[index].pointer) return nullptr;
        auto &block=win16_globals[index];
        if(auto *entry=selectors.Find(block.segment))block.locks=entry->locks;
        return &block;
    };
    auto sync=[&](Win16Global *block){
        if(block)if(auto *entry=selectors.Find(block->segment))entry->locks=block->locks;
    };
    if (ordinal == 24 || ordinal == 59) // GlobalAlloc16(flags, size), K32WOWGlobalAlloc16
    {
        const unsigned flags = a[0] & 0xffffu, size = a[1];
        unsigned index = 0;
        while (index < 64 && win16_globals[index].pointer) ++index;
        if (index == 64) return 0;
        const unsigned alloc[] = {(flags & K::GmemZeroInit) ? 0x40u : 0u, size ? size : 1};
        const unsigned pointer = Api("GlobalAlloc", alloc);
        if (!pointer) return 0;
        auto &block = win16_globals[index];
        block.pointer = pointer;
        block.size = size;
        block.flags = flags;
        block.locks = 0;
        block.handle = K::Win16Handle(index, (flags & K::GmemMoveable) != 0);
        return block.handle;
    }
    auto *block = find(a[0]);
    switch (ordinal)
    {
    case 25: // GlobalLock16: MapSL of the 16-bit result, a flat pointer or zero.
    case 29: // GlobalWire16
    case 60: // K32WOWGlobalLock16: the original answers a SEGPTR; see win98_segment_map.h.
        if (!block) return 0;
        if ((block->flags & K::GmemMoveable) && block->locks<255) ++block->locks;
        if(ordinal!=60){sync(block);return block->pointer;}
        if(!block->segment)block->segment=SegmentMap(block->pointer,true);
        sync(block);return block->segment;
    case 26: // GlobalUnlock16: zero once the count reaches zero
    case 30: // GlobalUnWire16: nonzero on success
        if (!block) return 0;
        if (block->locks && block->locks!=255) --block->locks;
        sync(block);
        return ordinal == 26 ? block->locks : 1;
    case 27: // GlobalFix16 and GlobalUnfix16 return nothing in KRNL386; the
    case 28: // register is left at zero here.
        if (block && (block->flags & K::GmemMoveable) && block->locks!=255)
            ordinal == 27 ? ++block->locks : (block->locks ? --block->locks : 0);
        sync(block);return 0;
    case 31: // GlobalFree16: zero on success, the handle otherwise
    {
        if (!block) return a[0] & 0xffffu;
        const unsigned release[] = {block->pointer};
        Api("GlobalFree", release);
        if(block->segment)selectors.Unmap(block->segment);
        *block = Win16Global{};
        return 0;
    }
    case 32: // GlobalSize16
        return block ? block->size : 0;
    }
    Fail("Unknown Win16 global heap ordinal");
    return 0;
}

// AllocMappedBuffer and FreeMappedBuffer use register arguments. EDI carries
// the byte count in and the buffer address out. An eight-byte header stores
// the allocation and mapped pointer. Preserve flags and ECX; EAX and EDX are
// scratch.
unsigned Application::MappedBufferApi(bool allocate)
{
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp), edi = Register(Reg::Edi);
    unsigned result = edi;
    if (allocate)
    {
        const unsigned block=Allocate(edi+8);
        result=0;
        if(block){
            const unsigned mapped=SegmentMap(block+8);
            if(mapped){
                const unsigned header[]={SegmentNode(mapped),mapped};
                if(Store(block,header,sizeof header))result=block+8;
            }
            if(!result)Free(block);
        }
    }
    else if(edi){
        const unsigned header=edi-8;
        SegmentUnmapNode(Read32(header));Free(header);result=header;
    }
    Register(Reg::Edi, result);
    Register(Reg::Esp, sp + 4);
    Register(Reg::Eip, ret);
    return 0;
}

// Ordinals 1..9. The service dword is a[0]; the service arguments follow it
// on the caller's stack and are removed here, as ring 0 does in Win98.
unsigned Application::VxDCallApi(const unsigned *a)
{
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp), service = a[0];
    const auto *entry = K::FindService(service);
    unsigned args[8] = {};
    if (entry) Read(sp + 8, args, entry->arguments * 4);
    unsigned value = 0, flags = Register(Reg::Eflags);
    if (log)
    {
        char text[256];
        snprintf(text, sizeof text, "VXDCALL service=%08x name=%s args=%08x,%08x,%08x,%08x,%08x call=%u", service,
                 entry ? entry->name : "unknown", args[0], args[1], args[2], args[3], args[4], api_calls);
        log(text);
    }
    if (!entry || !entry->served)
    {
        char message[200];
        snprintf(message, sizeof message, "VxDCall service %08x (%s) has no native provider", service,
                 entry ? entry->name : K::DeviceName(service));
        Fail(message);
        return 0;
    }
    switch (service)
    {
    case K::VmmPageReserve:
    {
        const unsigned base = K::ArenaRequest(args[0]) ? 0 : args[0] << 12;
        const unsigned reserve[] = {base, args[1] << 12, 0x2000, 1};
        const unsigned address = MemoryApi("VirtualAlloc", reserve);
        value = address ? address : K::PageFailure;
        break;
    }
    case K::VmmPageCommit:
    {
        const unsigned commit[] = {args[0] << 12, args[1] << 12, 0x1000, K::PageProtection(args[4])};
        value = MemoryApi("VirtualAlloc", commit) ? 1 : 0;
        break;
    }
    case K::VmmPageDecommit:
    {
        const unsigned decommit[] = {args[0] << 12, args[1] << 12, 0x4000};
        value = MemoryApi("VirtualFree", decommit);
        break;
    }
    case K::VmmPageFree:
    {
        const unsigned release[] = {args[0], 0, 0x8000};
        value = MemoryApi("VirtualFree", release);
        break;
    }
    case K::VmmPageModifyPermissions:
    {
        const unsigned old[] = {0};
        const unsigned scratch[] = {0, 4};
        const unsigned slot = Api("GlobalAlloc", scratch);
        if (!slot) { value = K::PageFailure; break; }
        Store(slot, old, 4);
        const unsigned protect[] = {args[0] << 12, args[1] << 12, K::PageProtection(args[3]), slot};
        const unsigned ok = MemoryApi("VirtualProtect", protect);
        const unsigned previous = Read32(slot);
        const unsigned release[] = {slot};
        Api("GlobalFree", release);
        value = ok ? ((previous == 4 || previous == 0x40) ? (K::PcUser | K::PcWriteable) : K::PcUser) : K::PageFailure;
        break;
    }
    case K::VmmRegFlushKey:
        // ADVAPI32's RegFlushKey is a plain forwarder to this service and
        // hands its result on unchanged, so the native registry answers here
        // with the same status the ADVAPI32 entry would return.
        value = RegistryApi("RegFlushKey", args);
        break;
    case K::Vwin32Sleep:
        value = ThreadApi("Sleep", args);
        break;
    case K::Vwin32PerformanceCounter:
        value = TimeApi("QueryPerformanceCounter", args);
        break;
    case K::Vwin32Int21Dispatch:
    {
        // args[0] is the caller's EAX, args[1] its ECX; EBX, EDX, ESI and EDI
        // are read from the registers. CF reports DOS failure.
        const unsigned function = (args[0] >> 8) & 0xff;
        unsigned eax = args[0], ebx = Register(Reg::Ebx), ecx = args[1], edx = Register(Reg::Edx);
        bool carry = false;
        const unsigned scratch_size[] = {0, 0x120};
        const unsigned scratch = Api("GlobalAlloc", scratch_size);
        if (!scratch) { Fail("Int21 scratch allocation failed"); return 0; }
        const unsigned directory[] = {0x104, scratch};
        char current[8] = {};
        const unsigned length = Api("GetCurrentDirectoryA", directory);
        if (length) Read(scratch, current, 3);
        if (function == K::Int21CurrentDrive)
        {
            eax = (eax & ~0xffu) | (length && current[1] == ':' ? (unsigned)((current[0] & 0xdf) - 'A') : 2);
        }
        else if (function == K::Int21FreeSpace)
        {
            const unsigned drive = edx & 0xff;
            char root[4] = {char(drive ? 'A' + drive - 1 : (length && current[1] == ':' ? (current[0] & 0xdf) : 'C')), ':', '\\', 0};
            Store(scratch, root, 4);
            const unsigned query[] = {scratch, scratch + 0x10, scratch + 0x14, scratch + 0x18, scratch + 0x1c};
            const unsigned saved = last_error;
            if (VolumeInformationApi("GetDiskFreeSpaceA", query))
            {
                // DOS reports 16-bit cluster counts. The native volume answers
                // byte counts (one byte per sector, one sector per cluster);
                // present them as 512-byte sectors with the smallest cluster
                // that keeps the total below 65536, capped like DOS at 0xffff.
                const unsigned long long unit = (unsigned long long)Read32(scratch + 0x10) * Read32(scratch + 0x14);
                unsigned long long free_bytes = unit * Read32(scratch + 0x18), total_bytes = unit * Read32(scratch + 0x1c);
                unsigned cluster = 512;
                while (total_bytes / cluster > 0xffffu && cluster < 0x8000u) cluster *= 2;
                auto clusters = [&](unsigned long long bytes) { unsigned long long n = bytes / cluster; return (unsigned)(n > 0xffffu ? 0xffffu : n); };
                eax = (eax & ~0xffffu) | (cluster / 512);
                ecx = (ecx & ~0xffffu) | 512;
                ebx = (ebx & ~0xffffu) | clusters(free_bytes);
                edx = (edx & ~0xffffu) | clusters(total_bytes);
            }
            else
            {
                last_error = saved;
                eax = (eax & ~0xffffu) | K::Int21InvalidDrive; // DOS reports an invalid drive in AX, without CF.
            }
        }
        else
        {
            const unsigned release[] = {scratch};
            Api("GlobalFree", release);
            char message[160];
            snprintf(message, sizeof message, "VWIN32 Int21Dispatch function %02x has no native provider", function);
            Fail(message);
            return 0;
        }
        const unsigned release[] = {scratch};
        Api("GlobalFree", release);
        Register(Reg::Ebx, ebx);
        Register(Reg::Ecx, ecx);
        Register(Reg::Edx, edx);
        flags = carry ? (flags | 1) : (flags & ~1u);
        value = eax;
        break;
    }
    }
    if (state == Failed || waiting) return 0;
    if (log)
    {
        char text[256];
        snprintf(text, sizeof text, "VXDRET service=%08x eax=%08x ebx=%08x ecx=%08x edx=%08x carry=%u", service, value,
                 Register(Reg::Ebx), Register(Reg::Ecx), Register(Reg::Edx), flags & 1);
        log(text);
    }
    Register(Reg::Eflags, flags);
    Register(Reg::Eax, value);
    Register(Reg::Esp, sp + 8 + entry->arguments * 4);
    Register(Reg::Eip, ret);
    return value;
}

// The stdcall ordinals of this family.
unsigned Application::KernelOrdinalApi(unsigned ordinal, const unsigned *a)
{
    switch (ordinal)
    {
    case 10: case 11: case 12: case 13:
        return OemStringApi(ordinal, a);
    case 14: // k32LoadStringA
        return K::LoadStringArguments(a[2], a[3]) ? Api("LoadStringA", a) : 0;
    case 15: // k32wsprintfA, cdecl: the arguments follow format on the caller's stack.
        return Printf(a[0], a[1], Register(Reg::Esp) + 12);
    case 16: // k32wvsprintfA
        return Printf(a[0], a[1], a[2]);
    case 19: // ThunkTheTemplateHandle: error 6 without a Win16 handle.
        last_error = 6;
        return 0;
    case 23: // GDIReallyCares(hModule): mark the module table entry of the
             // current process (Windows 98 sets bit 3 in the IMTE).
    {
        const unsigned index = ModuleIndex(a[0], true);
        if (index >= ModuleCapacity) return 0;
        modules[index].gdi_cares = true;
        return a[0];
    }
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 59: case 60:
        return Win16GlobalApi(ordinal, a);
    case 33: // HouseCleanLogicallyDeadHandles: no task data, so the flag path leaves at once.
        return Register(Reg::Eax);
    case 34: // GetWin16DOSEnv: the environment block the first process inherited.
        if (!win16_dos_environment)
        {
            const unsigned none[] = {0};
            win16_dos_environment = EnvironmentApi("GetEnvironmentStrings", none);
        }
        return win16_dos_environment;
    case 42: case 44: case 47: // Retail logging stubs, "ret 4".
        return Register(Reg::Eax);
    case 43: // ThunkInitLS(block, name16, checksum, dll16, dll32)
    {
        if (K::ThunkInitDirectPath(a[4]))
        {
            unsigned load = 0;
            if (a[0])
            {
                const unsigned dll16[] = {a[3]};
                load = Win16ModuleApi(Win98Win16Module::LoadLibrary16, dll16);
            }
            return K::ThunkInitDirectResult(a[0], load);
        }
        if (log)
        {
            char text[512];
            snprintf(text, sizeof text, "THUNKINITLS block=%08x name=%s dll16=%s dll32=%s call=%u", a[0],
                     ReadString(a[1]), ReadString(a[3]), ReadString(a[4]), api_calls);
            log(text);
        }
        ++win16_module_requests;
        return 0; // GetModuleHandle16 answers below 0x20 without a 16-bit loader.
    }
    case 46: // ThunkInitSL(block, name16, checksum, dll16?, dll16): Windows 98 answers zero below 0x20.
    case 52: // GetThunkBuff: SYSTHUNK.DLL!ThkBuf through the same 16-bit lookup.
    case 53: // GetThunkStuff(dll16, export16)
        ++win16_module_requests;
        return 0;
    case 48: // ReleaseThunkLock(count *)
    {
        // The original also left the Win16 mutex here. PLAN.md step 8 removed
        // that mutex; what stays is the word at fs:[0x1e], read out signed and
        // reset to 0xffff.
        if (!virtual_memory.Access(a[0], 4, true)) { last_error = K::InvalidParameter; return 0; }
        const unsigned count = ThunkLockCount();
        const unsigned stored = count & 0x8000 ? count | 0xffff0000u : count;
        ThunkLockCount(0xffff);
        Store(a[0], &stored, 4);
        return a[0];
    }
    case 49: // RestoreThunkLock(count): the counter without the mutex
        ThunkLockCount(a[0] & 65535);
        return ThreadId();
    case 54: // K32WOWCallback16: every NEO98 thread is a Win32-only thread.
        return 0;
    case 55: // K32WOWCallback16Ex(vpfn, flags, cbArgs, pArgs, pdwRetCode)
        if (a[2] > K::CallbackArgumentLimit) { last_error = K::InvalidParameter; return 0; }
        return 0;
    case 56: // K32WOWGetVDMPointer(vp, bytes, protected)
        if (!a[2]) return K::RealModePointer(a[0]);
        return (a[0]>>16)?selectors.Linear(a[0]):0;
    case 57: // K32WOWHandle32(handle16, type)
    case 58: // K32WOWHandle16(handle32, type)
        switch (K::WowHandleRule(a[1]))
        {
        case K::HandleUnsupported:
            last_error = K::NotSupported;
            return 0;
        case K::HandleTask:
            if (ordinal == 58 && a[0])
            {
                bool live = false;
                if (!FindProcessIdentity(a[0], live)) { last_error = K::InvalidParameter; return 0; }
            }
            return 0; // No task data: and the null-task branch answer zero.
        default:
            return a[0] & 0xffffu;
        }
    }
    Fail("Unknown KERNEL32 ordinal");
    return 0;
}

// Register entries that switch to a 16-bit stack. There is no 16-bit side;
// the run stops with the name of the transition, as the flat-thunk gates do.
unsigned Application::ThunkTransitionStop(unsigned ordinal)
{
    const char *name = ordinal == 40 ? "OT_32ThkLSF" : ordinal == 45 ? "Common32ThkLS" : "W32S_BackTo32";
    char message[160];
    snprintf(message, sizeof message, "KERNEL32 ordinal %u (%s) needs a 16-bit stack; NEO98 has no 16-bit side", ordinal, name);
    Fail(message);
    return 0;
}
} // namespace X86
