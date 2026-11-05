// SPDX-License-Identifier: GPL-2.0-or-later
// Native KERNEL32 pipe, mailslot, device, and socket services. Provide local
// mailslot queues, VxD device control, and DOS filesystem results without a
// network redirector.
#include "win98_ddraw_driver.h"
#include "x86_app.h"
#include "host_devices.h"
#include "host_media.h"
#include "win98_kernel_pipes.h"
#include "win98_mailslot.h"
#include <string.h>
#include <stdio.h>
namespace X86
{
struct Application::KernelPipeState
{
    enum Kind { Free = 0, Server = 1, Client = 2, Device = 3, Socket = 4 };
    struct Handle
    {
        unsigned kind = Free, generation = 1, object = 0, context = 0;
        bool inherit = false, overlapped = false;
        char name[Win98Mailslot::NameCapacity] = {};
    };
    // One byte of the handle value numbers the entry, so a process has up to
    // 255 mailslot, device and socket handles here.
    static constexpr unsigned Capacity = 255;
    Handle handles[Capacity];
    struct SocketObject { unsigned references = 0, context = 0; } socket_objects[Capacity];
    Win98Mailslot::Slot slots[16];
    unsigned socket_table[5] = {};
    bool sockets_loaded = false;
    // Socket handles are small numbers, as in the process handle table of
    // Win98: MSAFD.DLL keeps a byte per handle value (a bitmap of
    // handle + 80h bytes) and WS2HELP.DLL a context table sized by the value,
    // so a handle of the 66xxxxxxh form would ask them for 1.6 GB.
    static constexpr unsigned SocketBase = 0x200, SocketLimit = SocketBase + Capacity * 4;
    static bool SocketValue(unsigned value) { return value >= SocketBase && value < SocketLimit && !(value & 3); }
    unsigned SocketAlias(unsigned value) const { return SocketBase + ((value & 255) - 1) * 4; }
    // FILE_FLAG_GLOBAL_HANDLE (00800000h) of CreateFile, a flag only Windows
    // 9x has: the handle is good in every process. DSOUND.DLL opens its VxD
    // with it (4.06, flags 00800080h) and keeps the handle in its
    // shared data section; the first process that loads the DLL opens the
    // device, and every other one, DDHELP with the mixer thread among them,
    // calls DeviceIoControl on that same value. Such a handle lives
    // in a table of the session; bit 15 of its generation field tells it
    // from a handle of the process, whose generations stay below it.
    static constexpr unsigned GlobalCapacity = 16, GlobalMark = 0x8000;
    static Handle global_handles[GlobalCapacity];
    static Handle *FindGlobal(unsigned value)
    {
        const unsigned i = (value & 255) - 1, generation = (value >> 8) & 65535;
        if ((value & 0xff000000) != 0x66000000 || !(generation & GlobalMark) || i >= GlobalCapacity) return nullptr;
        return global_handles[i].kind && (global_handles[i].generation | GlobalMark) == generation ? &global_handles[i] : nullptr;
    }
    static unsigned AddGlobal(unsigned kind, const char *name)
    {
        for (unsigned i = 0; i < GlobalCapacity; ++i)
            if (!global_handles[i].kind)
            {
                auto &h = global_handles[i];
                h.kind = kind; h.object = 0; h.context = 0; h.inherit = false; h.overlapped = false;
                strncpy(h.name, name ? name : "", sizeof h.name - 1); h.name[sizeof h.name - 1] = 0;
                return 0x66000000 | ((h.generation | GlobalMark) << 8) | (i + 1);
            }
        return 0;
    }
    Handle *Find(unsigned value)
    {
        if (Handle *global = FindGlobal(value)) return global;
        if (SocketValue(value))
        {
            auto &h = handles[(value - SocketBase) / 4];
            return h.kind == Socket ? &h : nullptr;
        }
        const unsigned i = (value & 255) - 1;
        if ((value & 0xff000000) != 0x66000000 || i >= Capacity || !handles[i].kind) return nullptr;
        return handles[i].generation == ((value >> 8) & 65535) ? &handles[i] : nullptr;
    }
    unsigned Add(unsigned kind, unsigned object, const char *name, bool inherit)
    {
        for (unsigned i = 0; i < Capacity; ++i)
            if (!handles[i].kind)
            {
                auto &h = handles[i];
                h.kind = kind; h.object = object; h.context = 0; h.inherit = inherit; h.overlapped = false;
                strncpy(h.name, name ? name : "", sizeof h.name - 1); h.name[sizeof h.name - 1] = 0;
                return 0x66000000 | (h.generation << 8) | (i + 1);
            }
        return 0;
    }
    void Release(Handle &h)
    {
        if (h.kind == Server) slots[h.object] = Win98Mailslot::Slot();
        if (h.kind == Socket && !--socket_objects[h.object].references)
            socket_objects[h.object] = SocketObject{};
        h.kind = Free; h.generation = h.generation % 0x7ffe + 1; h.context = 0; h.name[0] = 0;
    }
};
Application::KernelPipeState::Handle Application::KernelPipeState::global_handles[Application::KernelPipeState::GlobalCapacity];
Application::KernelPipeOwner::~KernelPipeOwner() { delete state; }
static bool SameText(const char *a, const char *b) { return Win98KernelPipes::Prefix(a, b) && Win98KernelPipes::Length(a) == Win98KernelPipes::Length(b); }
bool Application::KernelPipeDeviceName(unsigned address)
{
    if (!address || !virtual_memory.Access(address, 4, false)) return false;
    const char *s = ReadString(address);
    return s[0] == '\\' && s[1] == '\\' && s[2] == '.' && s[3] == '\\';
}
bool Application::KernelSocketHandle(unsigned handle)
{
    return KernelPipeState::SocketValue(handle);
}
bool Application::OwnsKernelPipeHandle(unsigned handle) const
{
    return KernelPipeState::FindGlobal(handle) || (kernel_pipes.state && kernel_pipes.state->Find(handle));
}
// Scheduler probe for block 12: a mailslot read resumes when a message
// arrived or the handle disappeared; deadlines are handled by the caller.
bool Application::KernelPipeReady(unsigned handle) const
{
    auto *s = kernel_pipes.state;
    auto *h = s ? s->Find(handle) : nullptr;
    if (!h || h->kind != KernelPipeState::Server) return true;
    return s->slots[h->object].head != s->slots[h->object].tail;
}
bool Application::AnyGuestObject(unsigned handle)
{
    const unsigned space = handle & 0xff000000;
    if (space == 0x40000000) return files.Owns(handle);
    if (space == 0x63000000) return OwnsPipe(handle);
    if (space == 0x62000000) return console != nullptr;
    if (space == 0x60000000) return FindKernelHandle(handle) != nullptr;
    if (space == 0x66000000 || KernelSocketHandle(handle)) return OwnsKernelPipeHandle(handle);
    return false;
}
unsigned Application::KernelPipesApi(ApiName name, const unsigned *a)
{
    typedef KernelPipeState S;
    if (!kernel_pipes.state) kernel_pipes.state = new S;
    S &ps = *kernel_pipes.state;
    struct E
    {
        Application &app; S &ps;
        bool Writable(unsigned p, unsigned n) { return p && app.virtual_memory.Access(p, n, true); }
        void Write(unsigned p, unsigned v) { if (Writable(p, 4)) app.Write32(p, v); }
        unsigned Read(unsigned p) { return p && app.virtual_memory.Access(p, 4, false) ? app.Read32(p) : 0; }
        void Byte(unsigned p, unsigned v) { unsigned char b = (unsigned char)v; app.Store(p, &b, 1); }
        void Error(unsigned e) { app.last_error = e; }
        unsigned Drives() { const unsigned none[1] = {0}; return app.VolumeInformationApi("GetLogicalDrives", none); }
        // No SUBST in the RAM store: IFSMGR 71AAh answers carry for every drive.
        bool SubstQuery(unsigned, char *) { return false; }
        bool SubstRemove(unsigned) { return false; }
        bool SubstCreate(unsigned, const char *) { return false; }
        bool FullPath(const char *text, char *out)
        {
            if (!text || !*text) { Error(161); return false; }   //
            char full[260];
            const unsigned error = app.files.FullPath(text, full);
            if (error || strlen(full) >= 0x104) { Error(error ? error : 206); return false; }
            strcpy(out, full);
            return true;
        }
        unsigned SocketObject(unsigned handle)
        {
            auto *h = ps.Find(handle);
            if (h && h->kind == S::Socket) return 0;
            return app.AnyGuestObject(handle) ? 66 : 6;
        }
        unsigned SocketContext(unsigned handle) { return ps.socket_objects[ps.Find(handle)->object].context; }
        void SetSocketContext(unsigned handle, unsigned value) { ps.socket_objects[ps.Find(handle)->object].context = value; }
        unsigned PipeObject(unsigned handle, unsigned, unsigned &dos)
        {
            // Anonymous pipes are type-0x10 objects without a DOS handle; no
            // native object carries a named-pipe DOS handle.
            dos = ~0u;
            return app.OwnsPipe(handle) ? 12 : 6;
        }
        unsigned NetPipe(Win98KernelPipes::PipeRequest &r)
        {
            r.bytes = 0;
            if (r.handle != ~0u && r.name) {
                // IFSMGR 0x140ee80 / 0x140ed3b: the name must be UNC; the
                // resolver 0x14073b8 finds no network provider afterwards.
                const char *s = app.ReadString(r.name);
                if (s[0] != '\\' || s[1] != '\\') return 253;
                return 2;
            }
            return 1;
        }
        unsigned MailslotObject(unsigned handle)
        {
            auto *h = ps.Find(handle);
            return h && h->kind == S::Server ? 0 : 6;
        }
        unsigned MailslotCreate(unsigned name, unsigned maximum, unsigned timeout, bool inherit, unsigned &handle)
        {
            if (!name || !app.virtual_memory.Access(name, 1, false)) return 123;
            Win98Mailslot::Slot *slot = nullptr;
            const unsigned error = Win98Mailslot::Create(ps.slots, app.ReadString(name), maximum, timeout, slot);
            if (error) return error;
            handle = ps.Add(S::Server, (unsigned)(slot - ps.slots), slot->name, inherit);
            if (!handle) { *slot = Win98Mailslot::Slot(); return 4; }
            return 0;
        }
        unsigned MailslotInfo(unsigned handle, unsigned &maximum, unsigned &next, unsigned &count, unsigned &timeout)
        {
            Win98Mailslot::Info(ps.slots[ps.Find(handle)->object], maximum, next, count, timeout);
            return 0;
        }
        unsigned MailslotSetTimeout(unsigned handle, unsigned timeout) { ps.slots[ps.Find(handle)->object].timeout = timeout; return 0; }
        unsigned DeviceObject(unsigned handle) { return app.AnyGuestObject(handle) ? 0 : 6; }
        bool OverlappedHandle(unsigned handle) { auto *h = ps.Find(handle); return h && h->overlapped; }
        unsigned DeviceType(unsigned handle) { auto *h = ps.Find(handle); return h && h->kind == S::Device ? 0xf : 8; }
        // \\.\NEO98MEDIA, the host media provider (src/host_media.h): the
        // request is copied in, the answer out; the provider never sees a
        // guest address.
        unsigned MediaControl(const unsigned *a)
        {
            auto *media = HostMedia::current;
            if (!media) return 50;
            if (a[6] && !app.virtual_memory.Access(a[6], 4, true)) return 87;
            if (a[7]) return 87;
            HM_Request request{};
            const bool asks = a[1] != HM_IOCTL_STATUS && a[1] != HM_IOCTL_DIALOG;
            if (asks)
            {
                if (a[3] != sizeof request || !app.virtual_memory.Access(a[2], sizeof request, false)) return 87;
                app.Read(a[2], &request, sizeof request);
                if (request.version != HM_VERSION) return 87;
                request.path[sizeof request.path - 1] = 0;
            }
            unsigned error = 0, written = 0;
            auto answer = [&](auto *out) {
                if (a[5] < sizeof *out || !app.virtual_memory.Access(a[4], sizeof *out, true)) return 122u;
                if (!error) { app.Store(a[4], out, sizeof *out); written = sizeof *out; }
                return error;
            };
            if (a[1] == HM_IOCTL_STATUS)
            {
                HM_Status status{}; error = media->Status(status); error = answer(&status);
            }
            else if (a[1] == HM_IOCTL_BROWSE)
            {
                auto listing = new HM_Listing{}; error = media->Browse(request, *listing); error = answer(listing); delete listing;
            }
            else if (a[1] == HM_IOCTL_PICTURE)
            {
                auto picture = new HM_Picture{}; error = media->Picture(request, *picture); error = answer(picture); delete picture;
            }
            else if (a[1] == HM_IOCTL_CONTROL)
            {
                error = media->Control(request);
                // The answer to a sign-in may carry a password.
                if (request.action == HM_ANSWER) { volatile char *secret = request.path; for (unsigned i = 0; i < sizeof request.path; ++i) secret[i] = 0; }
            }
            else if (a[1] == HM_IOCTL_DIALOG)
            {
                auto dialog = new HM_Dialog{}; error = media->Dialog(*dialog); error = answer(dialog); delete dialog;
            }
            else return 50;
            if (!error && a[6]) app.Write32(a[6], written);
            return error;
        }
        unsigned VxdControl(unsigned handle, const unsigned *a)
        {
            auto *h = ps.Find(handle);
            if (h && SameText(h->name, "NEO98HOST"))
            {
                if (!HostDevices::current) return 50;
                unsigned written = 0, error = 0;
                if (a[6] && !app.virtual_memory.Access(a[6], 4, true)) return 87;
                if (a[7]) return 87;
                if (a[1] == AH_IOCTL_STATUS)
                {
                    if (a[5] < sizeof(AH_Status) || !app.virtual_memory.Access(a[4], sizeof(AH_Status), true)) return 122;
                    AH_Status status{}; error = HostDevices::current->Status(status);
                    if (!error) { app.Store(a[4], &status, sizeof status); written = sizeof status; }
                }
                else if (a[1] == AH_IOCTL_ACTION)
                {
                    if (a[3] != sizeof(AH_Request) || !app.virtual_memory.Access(a[2], sizeof(AH_Request), false)) return 87;
                    AH_Request request{};
                    app.Read(a[2], &request, sizeof request);
                    error = request.version == AH_VERSION ? HostDevices::current->Action(request) : 87;
                    volatile char *secret = request.password; for (unsigned i=0;i<sizeof request.password;++i) secret[i]=0;
                }
                else if (a[1] == AH_IOCTL_FRAME)
                {
                    if (a[5] < sizeof(AH_Frame) || !app.virtual_memory.Access(a[4], sizeof(AH_Frame), true)) return 122;
                    auto frame = new AH_Frame{};
                    error = HostDevices::current->Frame(frame, sizeof *frame, written);
                    if (!error && written <= sizeof *frame) app.Store(a[4], frame, written);
                    else if (!error) error = 13;
                    delete frame;
                }
                else return 50;
                if (!error && a[6]) app.Write32(a[6], written);
                return error;
            }
            if (h && SameText(h->name, HostMedia::DeviceName)) return MediaControl(a);
            if(h&&SameText(h->name,"CONFIGMG"))return app.ConfigManagerControl(a);
            if(h&&SameText(h->name,HostDevices::BatteryInterface))return app.BatteryControl(a);
            if (h && Win98DDrawDriver::DeviceName(h->name)) return app.DirectDrawVxdControl(a);
            if (h && SameText(h->name, "WSOCK2")) return app.Wsock2VxdControl(a);
            if (h && SameText(h->name, "VDHCP")) return app.VdhcpVxdControl(a);
            if (h && SameText(h->name, "VIP")) return app.VipVxdControl(a);
            if (h && SameText(h->name, "DINPUT.VXD")) return app.DinputVxdControl(a);
            if (h && SameText(h->name, "DSOUND.VXD")) return app.DsoundVxdControl(a);
            // The kernel's own lists (processes, VxDs) are not handed out.
            if (h && SameText(h->name, "MSISYS.VXD")) return 50;
            if (h && SameText(h->name, "HWINFOD.VXD"))
            {
                if (a[1] != 0x3e7 && a[1] != 0x3e8) return 50;
                if (a[5] < 8 || !app.virtual_memory.Access(a[4], a[5], true)) return 122;
                if (a[1] == 0x3e8 && (a[3] < 8 || !app.virtual_memory.Access(a[2], a[3], false))) return 87;
                const unsigned instance = a[1] == 0x3e8 ? app.Read32(a[2] + 4) : 0;
                app.Write32(a[4], 0xdeadbeef);
                const bool success = a[1] == 0x3e7 || Registry::PublishHardwareDiagnostic(app.registry.store, instance);
                app.Write32(a[4] + 4, success ? instance : 0);
                if (app.log) { char line[100]; snprintf(line, sizeof line, "HWINFOD code=%x instance=%u success=%u", a[1], instance, success); app.log(line); }
                if (a[6]) app.Write32(a[6], 8);
                return success ? 0 : 4;
            }
            return app.Vwin32DeviceControl(a);
        }
    } e{*this, ps};
    using namespace Win98KernelPipes;
    switch (name.id)
    {
    API_CASE("GetQueuedCompletionStatus") return GetQueuedCompletionStatus(e, a);
    API_CASE("GetHandleContext") return GetHandleContext(e, a);
    API_CASE("SetHandleContext") return SetHandleContext(e, a);
    API_CASE("CallNamedPipeA") return CallNamedPipeA(e, a);
    API_CASE("WaitNamedPipeA") return WaitNamedPipeA(e, a);
    API_CASE("TransactNamedPipe") return TransactNamedPipe(e, a);
    API_CASE("GetNamedPipeHandleStateA") return GetNamedPipeHandleStateA(e, a);
    API_CASE("SetNamedPipeHandleState") return SetNamedPipeHandleState(e, a);
    API_CASE("GetNamedPipeInfo") return GetNamedPipeInfo(e, a);
    API_CASE("CreateMailslotA") return CreateMailslotA(e, a);
    API_CASE("GetMailslotInfo") return GetMailslotInfo(e, a);
    API_CASE("SetMailslotInfo") return SetMailslotInfo(e, a);
    API_CASE("DeviceIoControl") return DeviceIoControl(e, a);
    API_CASE("QueryDosDeviceA")
    {
        const char *device = a[0] && virtual_memory.Access(a[0], 1, false) ? ReadString(a[0]) : nullptr;
        if (a[1] && a[2] && !virtual_memory.Access(a[1], a[2] > 0x104 ? 1 : a[2], true)) { last_error = 87; return 0; }
        return QueryDosDeviceA(e, device, a[1], a[2]);
    }
    API_CASE("DefineDosDeviceA")
    {
        const char *device = a[1] && virtual_memory.Access(a[1], 1, false) ? ReadString(a[1]) : nullptr;
        const char *target = a[2] && virtual_memory.Access(a[2], 1, false) ? ReadString(a[2]) : nullptr;
        return DefineDosDeviceA(e, a[0], device, target);
    }
    API_CASE("OpenVxDHandle")
    {
        // with mask 0x80000002: event objects only, one added reference.
        auto *h = FindKernelHandle(a[0]);
        if (!h || h->kind != 1 || events[h->object].timer) { last_error = 6; return 0; }
        ++events[h->object].references;
        return 0x8e000000u | (h->object + 1);
    }
    API_CASE("CloseSystemHandle")
    {
        // The close runs in the KERNEL32 process context (VWIN32 0x2A000D);
        // no native system-process handle table exists, so nothing resolves.
        last_error = 6;
        return 0;
    }
    API_CASE("CreateSocketHandle")
    {
        if (!ps.sockets_loaded)
        {
            const unsigned text = String("wsock32");
            if (!text) { last_error = 8; return 0; }
            const unsigned args[] = {text};
            unsigned module = ModuleApi("GetModuleHandleA", args);
            if (!module) module = ModuleApi("LoadLibraryA", args);
            Free(text);
            if (!module) return 0;   // no error of its own
            static const char *const names[] = {"recv", "Arecv", "send", "Asend", "closesockinfo"};
            for (unsigned i = 0; i < 5; ++i)
            {
                const unsigned symbol = String(names[i]);
                if (!symbol) { last_error = 8; return 0; }
                const unsigned lookup[] = {module, symbol};
                ps.socket_table[i] = ModuleApi("GetProcAddress", lookup);
                Free(symbol);
                if (!ps.socket_table[i]) return 0;
            }
            ps.sockets_loaded = true;
        }
        unsigned object = 0;
        while (object < S::Capacity && ps.socket_objects[object].references) ++object;
        if (object == S::Capacity) { last_error = 14; return 0; }
        const unsigned handle = ps.Add(S::Socket, object, nullptr, false);
        if (!handle) { last_error = 14; return 0; }
        ps.socket_objects[object] = {1, 0};
        return ps.SocketAlias(handle);
    }
    API_CASE("DuplicateHandle")
    {
        auto *source = ps.Find(a[1]);
        if (!source || source->kind != S::Socket) { last_error = 6; return 0; }
        if (!virtual_memory.Access(a[3], 4, true) || (a[6] & ~3u)) { last_error = 87; return 0; }
        const unsigned object = source->object;
        const unsigned handle = ps.Add(S::Socket, object, nullptr, a[5] != 0);
        if (!handle) { last_error = 8; return 0; }
        ++ps.socket_objects[object].references;
        Write32(a[3], ps.SocketAlias(handle));
        if (a[6] & 1) ps.Release(*source);
        return 1;
    }
    API_CASE("CreateFileA")
    {
        const char *path = ReadString(a[0]) + 4;
        // first tries IFSMGR, then opens the VxD. The
        // successful fallback retains that failed filesystem attempt's
        // error: 2 for valid dispositions, 87 otherwise. Mailslots are IFS.
        if(!Win98KernelPipes::Prefix(path,"MAILSLOT\\"))
            last_error=a[4]>=1 && a[4]<=5 ? 2 : 87;
        if (SameText(path,"NEO98HOST")||SameText(path,"CONFIGMG")||SameText(path,HostDevices::BatteryInterface))
        {
            if (!HostDevices::current||(SameText(path,HostDevices::BatteryInterface)&&!HostDevices::HasBattery())) { last_error=2; return ~0u; }
            const unsigned handle=ps.Add(S::Device,0,path,false);
            if (!handle) { last_error=4; return ~0u; }
            return handle;
        }
        if (SameText(path, HostMedia::DeviceName))
        {
            if (!HostMedia::current) { last_error = 2; return ~0u; }
            const unsigned handle = ps.Add(S::Device, 0, HostMedia::DeviceName, false);
            if (!handle) { last_error = 4; return ~0u; }
            return handle;
        }
        if (SameText(path, "VWIN32"))
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "VWIN32") : ps.Add(S::Device, 0, "VWIN32", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;   // FILE_FLAG_OVERLAPPED
            return handle;
        }
        // \\.\DDRAW.VXD, opened by DirectDrawCreate.
        // Windows 98 always has this VxD; without the handle the create
        // answers DDERR_GENERIC. See src/win98_ddraw_driver.h.
        if (Win98DDrawDriver::DeviceName(path))
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "DDRAW.VXD") : ps.Add(S::Device, 0, "DDRAW.VXD", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        // \\.\DINPUT.VXD, opened by DINPUT.DLL 4.06 when it
        // loads; the native provider serves the mouse (src/x86_dinput_vxd.cpp).
        if (SameText(path, "DINPUT.VXD"))
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "DINPUT.VXD") : ps.Add(S::Device, 0, "DINPUT.VXD", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        // \\.\DSOUND.VXD, opened by DSOUND.DLL when it loads.
        // With it DirectSound has a driver of its own instead of the wave
        // device (src/x86_dsound_vxd.cpp); a DSOUND.DLL that file does not
        // know finds no VxD, as on a machine with another DirectX.
        if (SameText(path, "DSOUND.VXD") && DsoundVxdOpen())
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "DSOUND.VXD") : ps.Add(S::Device, 0, "DSOUND.VXD", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        // \\.\MSISYS.VXD: the control of MSINFO32 that fills "System
        // Information" (MSISYS.OCX 10001AC2) writes a VxD of its own into the
        // system folder, opens it and ends the whole program when that fails.
        // The VxD hands out addresses inside VMM and KERNEL32 (the list of
        // processes, the list of VxDs) for three control codes; a native
        // system has none of them, so the device is there and answers each
        // code with ERROR_NOT_SUPPORTED, which the control takes as an empty
        // list.
        if (SameText(path, "MSISYS.VXD") || SameText(path, "HWINFOD.VXD"))
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, path) : ps.Add(S::Device, 0, path, false);
            if (!handle) { last_error = 4; return ~0u; }
            return handle;
        }
        // \\.\WSOCK2, opened by MSAFD.DLL (then \\.\WSOCK2.VXD).
        // The socket layer of Windows 98; without a network host it is
        // missing like on an installation without networking.
        if ((SameText(path, "WSOCK2") || SameText(path, "WSOCK2.VXD")) && Wsock2Available())
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "WSOCK2") : ps.Add(S::Device, 0, "WSOCK2", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        // \\.\VDHCP, opened by RNR20, MSWSOCK, WINIPCFG and DHCPCSVC.
        if ((SameText(path, "VDHCP") || SameText(path, "VDHCP.VXD")) && VdhcpAvailable())
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "VDHCP") : ps.Add(S::Device, 0, "VDHCP", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        // \\.\VIP, opened by IPHLPAPI (then \\.\VIP.VXD).
        if ((SameText(path, "VIP") || SameText(path, "VIP.VXD")) && VipAvailable())
        {
            const unsigned handle = a[5] & 0x00800000u ? S::AddGlobal(S::Device, "VIP") : ps.Add(S::Device, 0, "VIP", false);
            if (!handle) { last_error = 4; return ~0u; }
            ps.Find(handle)->overlapped = (a[5] & 0x40000000u) != 0;
            return handle;
        }
        if (Win98KernelPipes::Prefix(path, "MAILSLOT\\") && path[9])
        {
            // The local mailslot client endpoint only supports write access.
            if(a[1]&0x80000000u){last_error=5;return ~0u;}
            // IFSMGR binds a client handle to its slot at the first write
            // (0x140e6c2), so the open itself needs no existing server.
            char slot[Win98Mailslot::NameCapacity];
            if (Win98Mailslot::ParseName(ReadString(a[0]), slot)) { last_error = 123; return ~0u; }
            const unsigned handle = ps.Add(S::Client, 0, slot, false);
            if (!handle) { last_error = 4; return ~0u; }
            return handle;
        }
        // Other VxD names would load a driver; none is present.
        last_error = 2;
        return ~0u;
    }
    default:
        break;
    }
    auto *h = ps.Find(a[0]);
    if (!h) { last_error = 6; return 0; }
    switch (name.id)
    {
    API_CASE("CloseHandle")
    {
        // CloseHandle on a VxD handle sends DIOC_CLOSEHANDLE; WSOCK2 drops
        // the sockets of the process there.
        if (h->kind == S::Device && SameText(h->name, "WSOCK2")) Wsock2CloseDevice();
        if (h->kind == S::Device && SameText(h->name, "DINPUT.VXD")) FreeDinputVxd();
        if (h->kind == S::Device && SameText(h->name, "DSOUND.VXD")) FreeDsoundVxd();
        if (h->kind == S::Socket && ps.sockets_loaded && ps.socket_objects[h->object].references == 1)
        {
        // the destructor of a socket object: while WSOCK32 is
            // loaded, closesockinfo(context) runs before the object goes, and
            // that is how closesocket reaches MSAFD and WSOCK2 (ioctl 102h).
            const unsigned text = String("wsock32");
            unsigned module = 0;
            if (text)
            {
                const unsigned args[] = {text};
                module = ModuleApi("GetModuleHandleA", args);
                Free(text);
            }
            const unsigned context = ps.socket_objects[h->object].context, handle = a[0];
            if (module && context)
            {
                const unsigned args[] = {context};
                Call(ps.socket_table[4], args, 1);
            }
            h = ps.Find(handle);
            if (!h) return 1;
        }
        ps.Release(*h);
        return 1;
    }
    API_CASE("GetFileType") return 0;   // not a file, console, serial or pipe object
    API_CASE("WriteFile")
    {
        if (!e.Writable(a[3], 4)) { last_error = 87; return 0; }
        Write32(a[3], 0);
        if (h->kind != S::Client) { last_error = 5; return 0; }
        if (a[2] && !virtual_memory.Access(a[1], a[2], false)) { last_error = 998; return 0; }
        auto *slot = Win98Mailslot::Find(ps.slots, h->name);
        if (!slot) { last_error = 2; return 0; }
        unsigned char data[Win98Mailslot::RingSize];
        if (a[2] > sizeof data) { last_error = 121; return 0; }
        if (a[2] && !Read(a[1], data, a[2])) { last_error = 998; return 0; }
        const unsigned error = Win98Mailslot::Write(*slot, data, a[2]);
        if (error) { last_error = error; return 0; }
        Write32(a[3], a[2]);
        return 1;
    }
    API_CASE("ReadFile")
    {
        if (!e.Writable(a[3], 4)) { last_error = 87; return 0; }
        Write32(a[3], 0);
        if (h->kind != S::Server) { last_error = 5; return 0; }
        if (a[2] && !virtual_memory.Access(a[1], a[2], true)) { last_error = 998; return 0; }
        const unsigned handle = a[0];
        auto &slot = ps.slots[h->object];
        if (slot.pending) { last_error = 8; return 0; }
        const unsigned capacity = a[2] ? a[2] : slot.maximum;
        const unsigned timeout = slot.timeout;
        const auto deadline = timeout == ~0u || !timeout ? 0 : Now() + (unsigned long long)timeout * 1000;
        slot.pending = handle;
        for (;;)
        {
            h = ps.Find(handle);
            if (!h) { last_error = 6; return 0; }
            unsigned char data[Win98Mailslot::RingSize];
            unsigned length = 0;
            const unsigned result = Win98Mailslot::Read(ps.slots[h->object], data, capacity, length);
            if (result != Win98Mailslot::Empty)
            {
                ps.slots[h->object].pending = 0;
                if (result) { last_error = result; return 0; }
                if (length && !Store(a[1], data, length)) { last_error = 998; return 0; }
                Write32(a[3], length);
                return 1;
            }
            if (!timeout) { ps.slots[h->object].pending = 0; return 1; }
            if (deadline && Now() >= deadline) { ps.slots[h->object].pending = 0; last_error = 121; return 0; }
            auto &t = threads[current_thread];
            t.block = 12; t.wait_handle = handle; t.deadline = deadline;
            Suspend(Idle);
            t.block = 0; t.deadline = 0;
            if (state == Exited || state == Failed) { last_error = 995; return 0; }
        }
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
// VWIN32 service 0x2A001F for the "\\.\VWIN32" device: DIOC_REGISTERS in and
// out (EBX, EDX, ECX, EAX, EDI, ESI, Flags). INT 21h answers come from the
// RAM store's single drive; everything else carries with AX = 1.
unsigned Application::Vwin32DeviceControl(const unsigned *a)
{
    const unsigned code = a[1], in = a[2], in_size = a[3], out = a[4], out_size = a[5];
    const auto returned = [&](unsigned n) { if (a[6] && virtual_memory.Access(a[6], 4, true)) Write32(a[6], n); };
    if (code == ~0u) { returned(0); return 0; }   // DIOC_CLOSEHANDLE
    if (code > 6) return 50;
    if (in_size < 28 || out_size < 28 || !virtual_memory.Access(in, 28, false) || !virtual_memory.Access(out, 28, true)) return 87;
    unsigned r[7];
    for (unsigned i = 0; i < 7; ++i) r[i] = Read32(in + i * 4);
    unsigned &ebx = r[0], &edx = r[1], &ecx = r[2], &eax = r[3], &edi = r[4], &esi = r[5], &flags = r[6];
    const unsigned drives = VolumeInformationApi("GetLogicalDrives", r);
    const auto carry = [&](unsigned error) { eax = error; flags |= 1; };
    flags &= ~1u;
    const auto drive_ok = [&](unsigned number) { return number && number <= 26 && ((drives >> (number - 1)) & 1); };
    if (code == 1)
    {
        const unsigned function = eax & 0xffff, drive = (ebx & 0xff) ? (ebx & 0xff) : 3;
        if (function == 0x4408) { if (!drive_ok(drive)) carry(15); else eax = 1; }
        else if (function == 0x4409) { if (!drive_ok(drive)) carry(15); else { eax = 0; edx = 0; } }
        else if (function == 0x440d && (ecx & 0xff) == 0x66 && ((ecx >> 8) & 0xff) == 8)
        {
            if (!drive_ok(drive)) carry(15);
            else if (!virtual_memory.Access(edx, 25, true)) carry(87);
            else
            {
                unsigned char mid[25] = {};
                memcpy(mid + 6, "           ", 11);
                memcpy(mid + 6, files.store.label, strlen(files.store.label) > 11 ? 11 : strlen(files.store.label));
                memcpy(mid + 17, "NEO98   ", 8);
                Store(edx, mid, sizeof mid);
                eax = 0;
            }
        }
        else carry(1);
    }
    else if (code == 6)
    {
        if ((eax & 0xffff) == 0x7303)
        {
            const char *root = virtual_memory.Access(edx, 3, false) ? ReadString(edx) : "";
            const bool ok = root[0] && root[1] == ':' && ((root[0] & 0xdf) == 'C') && (!root[2] || (root[2] == '\\' && !root[3]));
            if (!ok) carry(15);
            else if ((ecx & 0xffff) < 44 || !virtual_memory.Access(edi, 44, true)) carry(87);
            else
            {
                unsigned total = 0, available = 0;
                files.store.DataSpace(total, available);
                const unsigned info[11] = {44 | (0u << 16), 1, 1, available, total, available, total, available, total, 0, 0};
                Store(edi, info, sizeof info);
                eax = 0;
            }
        }
        else carry(1);
    }
    else carry(1);   // INT 25h/26h/13h raw sector access is absent in the RAM store
    Store(out, r, sizeof r);
    returned(28);
    return 0;
}

unsigned Application::BatteryControl(const unsigned *a)
{
    if(!HostDevices::HasBattery())return 1167;
    if(a[7]||(a[6]&&!virtual_memory.Access(a[6],4,true)))return 87;
    unsigned data[8]={},bytes=0;const auto power=HostDevices::Power();
    const unsigned input=a[1]==0x294040?4:a[1]==0x294044?12:a[1]==0x29404c?20:0;
    if(!input)return 50;
    if(a[3]<input||!virtual_memory.Access(a[2],input,false))return 87;
    if(a[1]!=0x294040&&Read32(a[2])!=1)return 2;
    if(a[1]==0x294040){data[0]=1;bytes=4;}
    else if(a[1]==0x29404c)
    {data[0]=(power.ac==1?1:0)|(power.ac==0?2:0)|((power.flags&8)?4:0)|((power.flags&4)?8:0);data[1]=power.percent==255?~0u:power.percent;data[2]=~0u;data[3]=0x80000000;bytes=16;}
    else switch(Read32(a[2]+4))
    {
        case 0:data[0]=0xc0000000u;data[1]=1;memcpy(data+2,"    ",4);data[3]=data[4]=100;bytes=28;break;
        case 1:data[0]=1;data[1]=100;bytes=8;break;
        case 3:data[0]=power.seconds;bytes=4;break;
        case 4:case 6:case 7:case 8:
        {
            const char *text="NEO98 Host Battery";unsigned short wide[32]={};unsigned n=0;for(;text[n];++n)wide[n]=(unsigned char)text[n];bytes=(n+1)*2;
            if(a[5]<bytes)return 122;if(!Store(a[4],wide,bytes))return 87;if(a[6])Write32(a[6],bytes);return 0;
        }
        default:return 50;
    }
    if(a[5]<bytes)return 122;if(!Store(a[4],data,bytes))return 87;if(a[6])Write32(a[6],bytes);return 0;
}
} // namespace X86
