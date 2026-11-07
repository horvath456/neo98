// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for KERNEL32 fibers, debugging, power, and system-message
// services.
#include "win98_fibers.h"
#include "win98_nt_status.h"
namespace X86 {
namespace {
void WriteWatchHook(Cpu &, unsigned address, unsigned size, void *user)
{
    static_cast<Application *>(user)->MarkWriteWatch(address, size);
}
constexpr unsigned WriteWatchFailure = 0xffffffffu;
// KERNEL32 string table 0x65 and 0x66 of the original DLL; NEO98's native
// module carries no resource directory to load them from.
const char FatalCaption[] = "Program Error";
const char FatalText[] = "Closing current program.\nIf the problem persists, contact the vendor for this program.";
} // namespace
unsigned Application::FiberApi(ApiName name, const unsigned *a)
{
    struct Environment
    {
        Application &app;
        unsigned Read(unsigned p) { return app.Read32(p); }
        void Write(unsigned p, unsigned v) { app.Write32(p, v); }
        unsigned Tib() { return app.Teb(); }
        unsigned Reg(int r) { return app.Register(Id(r)); }
        void SetReg(int r, unsigned v) { app.Register(Id(r), v); }
        static int Id(int r)
        {
            const int ids[] = {Reg::Ebx, Reg::Esi, Reg::Edi, Reg::Ebp, Reg::Esp, Reg::Eip};
            return ids[r];
        }
        void Error(unsigned e) { app.last_error = e; }
        bool Win16Process() { return app.process_flags & 8; }
        unsigned Heap() { return app.HeapApi("GetProcessHeap", nullptr); }
        unsigned AllocateBlock(unsigned size)
        {
            unsigned args[] = {Heap(), 0, size};
            return app.HeapApi("HeapAlloc", args);
        }
        void FreeBlock(unsigned block)
        {
            unsigned args[] = {Heap(), 0, block};
            app.HeapApi("HeapFree", args);
        }
        // The module default of the original (image stack reserve) is the same
        // 1 MB the native CreateThread uses for a zero request.
        unsigned DefaultStackSize() { return 0x100000; }
        unsigned AllocateStack(unsigned size, unsigned &base, unsigned &limit)
        {
            unsigned args[] = {0, size, 0x3000, 4};
            const unsigned region = app.MemoryApi("VirtualAlloc", args);
            if (!region) return 0;
            limit = region;
            base = region + ((size + 4095) & ~4095u);
            return region;
        }
        void FreeStack(unsigned object)
        {
            if (!object) return; // A converted thread's own stack has no object here.
            unsigned args[] = {object, 0, 0x8000};
            app.MemoryApi("VirtualFree", args);
        }
        // The original thunk Windows 98 runs the routine and calls ExitThread
        // with its result. The native return gate ends the thread the same way.
        void StartFrame(unsigned base, unsigned start, unsigned parameter, unsigned &esp, unsigned &eip)
        {
            esp = base - 8;
            app.Write32(esp, Exit);
            app.Write32(esp + 4, parameter);
            eip = start;
        }
        void ExitThread(unsigned code) { app.ThreadApi("ExitThread", &code); }
    } e{*this};
    switch (name.id)
    {
    API_CASE("ConvertThreadToFiber") return Win98Fibers::Convert(e, a[0]);
    API_CASE("CreateFiber") return Win98Fibers::Create(e, a[0], a[1], a[2]);
    API_CASE("DeleteFiber")
    {
        if (!virtual_memory.Access(a[0], Win98Fibers::BlockSize, false)) { Fail("DeleteFiber block is not readable guest memory"); return 0; }
        Win98Fibers::Delete(e, a[0]);
        return 0;
    }
    API_CASE("SwitchToFiber")
    {
        // The original reads the current block through FS:[0x10] and the target
        // through its argument; either fault would be an access violation.
        if (!virtual_memory.Access(a[0], Win98Fibers::BlockSize, true)) { Fail("SwitchToFiber target is not writable guest memory"); return 0; }
        if (!virtual_memory.Access(Read32(Teb() + Win98Fibers::TibFiberData), Win98Fibers::BlockSize, true)) { Fail("SwitchToFiber on a thread that is not a fiber"); return 0; }
        const unsigned sp = Register(Reg::Esp);
        Win98Fibers::Switch(e, a[0], sp, Read32(sp));
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
bool Application::RegisterWriteWatch(unsigned base, unsigned pages)
{
    if (write_watch_count >= sizeof write_watches / sizeof write_watches[0]) return false;
    auto *dirty = static_cast<unsigned char *>(malloc(pages));
    if (!dirty) return false;
    memset(dirty, 0, pages);
    if (!write_watch_count)
    {
        if (cpu->WatchWrites(WriteWatchHook, this) != CpuError::Ok) { free(dirty); return false; }
        write_watch_hook = 1;
    }
    write_watches[write_watch_count++] = {base, pages, dirty};
    // An engine that watches by page protection starts with the region.
    cpu->RearmWrites(base, pages * 4096);
    return true;
}
void Application::ReleaseWriteWatch(unsigned base)
{
    for (unsigned i = 0; i < write_watch_count; ++i)
        if (write_watches[i].base == base)
        {
            free(write_watches[i].dirty);
            write_watches[i] = write_watches[--write_watch_count];
            write_watches[write_watch_count] = {};
            if (!write_watch_count && cpu) { cpu->UnwatchWrites(); write_watch_hook = 0; }
            return;
        }
}
void Application::FreeWriteWatches()
{
    while (write_watch_count) ReleaseWriteWatch(write_watches[write_watch_count - 1].base);
}
void Application::MarkWriteWatch(unsigned address, unsigned size)
{
    if (!size) return;
    const unsigned first = address >> 12, last = (unsigned)(((unsigned long long)address + size - 1) >> 12);
    for (unsigned i = 0; i < write_watch_count; ++i)
    {
        auto &w = write_watches[i];
        const unsigned start = w.base >> 12, end = start + w.pages;
        if (last < start || first >= end) continue;
        for (unsigned page = first < start ? start : first; page <= last && page < end; ++page) w.dirty[page - start] = 1;
    }
}
unsigned Application::WriteWatchApi(ApiName name, const unsigned *a)
{
    const bool get = API_IS(name,"GetWriteWatch");
    const unsigned base = a[get ? 1 : 0], size = a[get ? 2 : 1];
    if (get && (!virtual_memory.Access(a[4], 4, true) || !virtual_memory.Access(a[5], 4, true)))
    { Fail("GetWriteWatch count/granularity probe violates guest memory"); return WriteWatchFailure; }
    // The VMM services 0x1002C/0x1002D are ring-0 providers. Their failure
    // value and error code are not reconstructable; the native provider
    // answers -1 and error 87 for a span outside one watched region.
    WriteWatch *watch = nullptr;
    const unsigned long long end = (unsigned long long)base + size;
    for (unsigned i = 0; i < write_watch_count && !watch; ++i)
        if (base >= write_watches[i].base && end <= (unsigned long long)write_watches[i].base + write_watches[i].pages * 4096ull) watch = &write_watches[i];
    if (!watch || !size) { last_error = 87; return WriteWatchFailure; }
    const unsigned first = (base - watch->base) >> 12, last = (unsigned)((end - 1 - watch->base) >> 12);
    if (!get)
    {
        for (unsigned page = first; page <= last; ++page) watch->dirty[page] = 0;
        cpu->RearmWrites(watch->base + first * 4096, (last - first + 1) * 4096);
        return 0;
    }
    const unsigned capacity = Read32(a[4]);
    if (capacity && !virtual_memory.Access(a[3], capacity * 4, true)) { last_error = 87; return WriteWatchFailure; }
    unsigned written = 0;
    for (unsigned page = first; page <= last && written < capacity; ++page)
        if (watch->dirty[page])
        {
            Write32(a[3] + written * 4, watch->base + page * 4096);
            if (a[0] & 1) watch->dirty[page] = 0;
            ++written;
        }
    Write32(a[4], written);
    Write32(a[5], 4096);
    if (a[0] & 1) cpu->RearmWrites(watch->base + first * 4096, (last - first + 1) * 4096);
    return 0;
}
unsigned Application::FatalAppExit(unsigned text)
{
    const bool debugger = false; // No debugger provider is attached to a guest.
    unsigned exit_code = Win98KernelMisc::FatalExitCode;
    if (!Win98KernelMisc::FatalTerminatesSilently(process_flags, legacy_error_mode, debugger))
    {
        char body[512];
        if (text) { const char *s = ReadString(text); size_t n = strlen(s); if (n >= sizeof body) n = sizeof body - 1; memcpy(body, s, n); body[n] = 0; }
        if (log) { char line[600]; snprintf(line, sizeof line, "FatalAppExit %s", text ? body : FatalText); log(line); }
        // SysErrorBox with a single default Close button; the native route is
        // the ordinary message box. Only a debugger's Cancel returns.
        const unsigned result = MessageBox(0, text ? body : FatalText, FatalCaption, 0);
        if (state == Failed || state == Exited) return 0;
        if (Win98KernelMisc::FatalReturns(legacy_error_mode, debugger, debugger ? result : 2)) return 0;
    }
    Api("ExitProcess", &exit_code);
    return 0;
}
unsigned Application::MiscFamilyApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    API_CASE("ConvertThreadToFiber") API_CASE("CreateFiber") API_CASE("DeleteFiber") API_CASE("SwitchToFiber") return FiberApi(name, a);
    API_CASE("GetWriteWatch") API_CASE("ResetWriteWatch") return WriteWatchApi(name, a);
    API_CASE("TlsAllocInternal") return TlsApi("TlsAlloc", a);
    API_CASE("TlsFreeInternal") return TlsApi("TlsFree", a);
    API_CASE("K32_RtlNtStatusToDosError") return Win98NtStatus::ToDosError(a[0]);
    API_CASE("FatalAppExitA") return FatalAppExit(a[1]);
    API_CASE("FatalExit")
    {
        // DebugBreak first, then the box with no text and exit -1.
        ExceptionApi("DebugBreak", a);
        if (state == Failed || state == Exited) return 0;
        return FatalAppExit(0);
    }
    API_CASE("_DebugOut")
    {
        if (Win98KernelMisc::DebugPrints(a[0]) && log)
        {
            char line[600];
            Win98KernelMisc::DebugLine(line, sizeof line - 2, a[0], a[1] ? ReadString(a[1]) : "");
            line[strlen(line) - 2] = 0; // The log adds its own line end.
            log(line);
        }
        return 0; // A debugger break for levels 0 and 1 needs an attached debugger.
    }
    API_CASE("_DebugPrintf") API_CASE("dprintf") return 0; // retail body returns 0 without output.
    API_CASE("Beep")
    {
        // plays USER32 PlaySoundEvent(0x91), then 0xFFFF, and answers 1
        // whatever happens; neither call reads the frequency or the duration.
        // Both end in the speaker routine USER, the same one MessageBeep
        // reaches, so this is the same tone: 748 Hz for the length of its delay
        // loop.
        Sound::Speaker::Tone(748, 40);
        return 1;
    }
    API_CASE("ContinueDebugEvent") API_CASE("WaitForDebugEvent") return DebugApi(name,a);
    API_CASE("DebugActiveProcess")
    {
        // An unknown id is error 87. A known process would need the
        // VWIN32 attach service; that provider is absent, so the attach failure
        // path (also 87) answers.
        if (log && (a[0] == process_id || User::ProcessContextOf(a[0]))) log("DebugActiveProcess: no debugger attach provider");
        last_error = 87;
        return 0;
    }
    API_CASE("GetThreadSelectorEntry")
    {
        if (!virtual_memory.Access(a[2], 8, true)) { Fail("GetThreadSelectorEntry entry probe violates guest memory"); return 0; }
        if (a[0] != 0xfffffffe) { auto *h = FindKernelHandle(a[0]); if (!h || h->kind != 2) { last_error = 6; return 0; } }
        // demands an LDT selector and bounds it by the LDT limit. NEO98
        // runs its guest on GDT selectors and has no LDT, so the table check
        // applies to the native descriptor table instead.
        const unsigned index = a[1] & 0xfff8;
        if ((a[1] & 4) || index >= (3 + ThreadCapacity) * 8) { last_error = 87; return 0; }
        unsigned char descriptor[8];
        if (!Read(SystemPageBase + index, descriptor, 8)) return 0;
        return Store(a[2], descriptor, 8) ? 1 : 0;
    }
    API_CASE("CreateKernelThread")
    {
        if (a[0] && !virtual_memory.Access(a[0], 12, false)) { Fail("CreateKernelThread attributes probe violates guest memory"); return 0; }
        if (!virtual_memory.Access(a[5], 4, true)) { Fail("CreateKernelThread thread-id probe violates guest memory"); return 0; }
        if ((a[0] && Read32(a[0]) != 12) || !a[2]) { last_error = 87; return 0; }
        // creates the ring-0 thread through VWIN32 0x2A000D. No such
        // provider exists; the native answer is a documented ERROR_NOT_SUPPORTED.
        if (log) log("CreateKernelThread: ring-0 thread provider unavailable");
        last_error = 50;
        return 0;
    }
    API_CASE("GetProductName")
    {
        struct Environment { Application &app; void Byte(unsigned p, char c) { app.Store(p, &c, 1); } } e{*this};
        const unsigned size = a[1] & 0xffff;
        if (size && !virtual_memory.Access(a[0], size < 12 ? size : 12, true)) { Fail("GetProductName buffer probe violates guest memory"); return 0; }
        return Win98KernelMisc::ProductNameCopy(e, a[0], size);
    }
    API_CASE("K32_NtCreateFile") return 0xc0000002u; // NTKERN service 0x4B0001 is absent: STATUS_NOT_IMPLEMENTED.
    API_CASE("NotifyNLSUserCache")
    {
        // only the user default locale with an active cache and a
        // text not starting with 0xFF reaches the SetLocaleInfoA body.
        if (!virtual_memory.Access(a[2], 1, false)) { Fail("NotifyNLSUserCache text probe violates guest memory"); return 0; }
        unsigned char first = 0;
        Read(a[2], &first, 1);
        if (a[0] != Api("GetUserDefaultLCID", nullptr) || first == 0xff) return 0;
        Api("SetLocaleInfoA", a);
        return 1;
    }
    API_CASE("ResetNLSUserInfoCache")
    {
        // reloads the user cache from HKCU\Control Panel\International
        // and answers 1 when the key opened, 0 otherwise. NEO98 reads the
        // registry on each query, so only the key's presence is reported.
        const unsigned path = String("Control Panel\\International"), slot = Allocate(4);
        if (!path || !slot) { if (path) Free(path); if (slot) Free(slot); last_error = 8; return 0; }
        unsigned args[] = {0x80000001u, path, 0, 0x20019, slot};
        const unsigned opened = Api("RegOpenKeyExA", args) == 0;
        if (opened) { unsigned key = Read32(slot); Api("RegCloseKey", &key); }
        Free(path);
        Free(slot);
        return opened ? 1 : 0;
    }
    API_CASE("RegisterSysMsgHandler")
    {
        if (!Win98KernelMisc::SysMsgRegisterFlagsValid(a[4])) { last_error = 87; return 0; }
        if (a[1] != 0xfffffffe) { auto *h = FindKernelHandle(a[1]); if (!h || h->kind != 2) { last_error = 6; return 0; } }
        const unsigned thread = a[1] == 0xfffffffe ? ThreadId() : threads[FindKernelHandle(a[1])->object].id;
        if (a[4] & 1) return sys_msg_handlers.Unregister(a[0], thread, a[2], a[3]);
        if (!sys_msg_handlers.Register(a[0], thread, a[2], a[3], a[4])) { last_error = 8; return 0; }
        return 1;
    }
    API_CASE("SignalSysMsgHandlers")
    {
        if (!Win98KernelMisc::SysMsgSignalFlagsValid(a[3])) { last_error = 87; return Win98KernelMisc::SysMsgFailure; }
        // presets error 8 and keeps it when the VWIN32 delivery
        // service 0x2A0055 fails. NEO98 has no delivery provider for a matching
        // handler, so that failure path answers; no match ends with error 0.
        if (sys_msg_handlers.Matching(a[0], a[3]))
        {
            if (log) log("SignalSysMsgHandlers: no VWIN32 delivery provider for a registered handler");
            last_error = 8;
            return Win98KernelMisc::SysMsgFailure;
        }
        last_error = 0;
        return 0;
    }
    API_CASE("SetMessageWaitingIndicator") { last_error = 87; return 0; } // no type-21 objects exist here.
    API_CASE("RequestWakeupLatency")
    {
        struct Environment
        {
            Application &app;
            bool Flag() { return app.wakeup_latency_flag; }
            void Flag(bool v) { app.wakeup_latency_flag = v; }
            unsigned Count() { return wakeup_latency_count; }
            void Count(unsigned v) { wakeup_latency_count = v; }
            // NTKERN 0x4B0009 on a platform without wake-latency hardware: the
            // request is accepted and has no host effect.
            unsigned Driver(bool) { return 0; }
            void Error(unsigned e) { app.last_error = e; }
        } e{*this};
        return Win98KernelMisc::WakeupLatency(e, a[0]);
    }
    API_CASE("RequestDeviceWakeup") API_CASE("CancelDeviceWakeupRequest")
    {
        // /Windows 98 accept file, device and NTKERN handles; other
        // handles are error 87. A plain file has no wake-capable device, which
        // the original answers with 0 and an unchanged error.
        const unsigned saved = last_error;
        if (!Api("GetFileType", a)) { last_error = 87; return 0; }
        last_error = saved;
        return 0;
    }
    API_CASE("GetDevicePowerState")
    {
        if (!virtual_memory.Access(a[1], 1, true)) { Fail("GetDevicePowerState state probe violates guest memory"); return 0; }
        last_error = 87; // no device object behind any handle here.
        return 0;
    }
    API_CASE("SetSystemPowerState")
    {
        // broadcasts to VxDs and maps the returned status. Without a
        // power driver the request fails rather than reporting a suspend.
        last_error = Win98KernelMisc::PowerStateError(0x80000001u);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
unsigned Application::wakeup_latency_count = 0;
} // namespace X86
