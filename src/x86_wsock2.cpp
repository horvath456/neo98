// SPDX-License-Identifier: GPL-2.0-or-later
// The native WSOCK2.VXD: the socket layer of Windows 98 under MSAFD.DLL, on
// the sockets of the host.
// Under Win98 a Winsock call runs WSOCK32.DLL or WS2_32.DLL, the catalog
// provider MSAFD.DLL and then DeviceIoControl on \\.\WSOCK2 with the codes
// 100h..11Ch. WSOCK2.VXD keeps the socket objects, the waits of blocking
// calls, select, WSAAsyncSelect and WSAEventSelect, and hands the transport to
// the address-family provider AFVXD.VXD, which speaks TDI to the MSTCP stack.
// The host sockets replace the AFVXD and MSTCP transport providers;  the transport indications of TDI become a poll of the host
// socket (TickNetwork).
// The socket object is the WSOCK2 object itself. MSAFD keeps its address as
// the handle context and reads the address family from it, and
// 11Bh copies its first ECh bytes to ring 3, so it lives in memory every
// process sees: a system block of the session (src/x86_session_arena.cpp).
// The host writes it, the guest only reads.
#include "neo98_hardware.h"
#include "x86_app.h"
#include "x86_state_parts.h"
#include "net_host.h"
#include "user.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace NetHost
{
static Sockets *installed = nullptr;
Sockets *Current() { return installed; }
void Install(Sockets *sockets) { installed = sockets; }
}
namespace X86
{
namespace
{
typedef unsigned U;
// WSAE* codes as WSOCK2 returns them through DeviceIoControl.
enum : U
{
    Wsaeintr = 0x2714, Wsaeacces = 0x271d, Wsaefault = 0x271e, Wsaeinval = 0x2726, Wsaewouldblock = 0x2733,
    Wsaenotsock = 0x2736, Wsaemsgsize = 0x2738, Wsaenoprotoopt = 0x273a, Wsaeprotonosupport = 0x273b,
    Wsaesocktnosupport = 0x273c, Wsaeopnotsupp = 0x273d, Wsaeafnosupport = 0x273f, Wsaeaddrinuse = 0x2740,
    Wsaeaddrnotavail = 0x2741, Wsaenetdown = 0x2742, Wsaenetunreach = 0x2743, Wsaeconnaborted = 0x2745,
    Wsaeconnreset = 0x2746, Wsaenobufs = 0x2747, Wsaeisconn = 0x2748, Wsaenotconn = 0x2749,
    Wsaeshutdown = 0x274a, Wsaetimedout = 0x274c, Wsaeconnrefused = 0x274d, Wsaehostunreach = 0x2751,
    Wsanotinitialised = 0x276d, Wsasyscallfailure = 0x277b,
    // The handler answers 0FFFFh when the caller has to wait for the APC
    // in Windows 98, 3E5h (ERROR_IO_PENDING) for an overlapped request.
    Pending = 0xffff,
    // The APC parameter of a wait that closesocket aborted.
    Aborted = 0xfffe,
};
enum : U { FdRead = 1, FdWrite = 2, FdOob = 4, FdAccept = 8, FdConnect = 0x10, FdClose = 0x20,
           ConnectFailed = 0x400 };
// The WSOCK2 socket object has a 0ECh-byte header.
// The provider stores the local and remote addresses after it.
enum : U
{
    OLink = 0x00, OWaiters = 0x08, OLinger = 0x10, OLocal = 0x14, ORemote = 0x18, OProvider = 0x1c,
    OLocalLength = 0x20, ORemoteLength = 0x24, OFamily = 0x28, OType = 0x2c, OProtocol = 0x30,
    OSendBuffer = 0x34, OReceiveBuffer = 0x38, OFlags = 0x3c, OOptions = 0x40, OBacklog = 0x44,
    OState = 0x4c, OEvents = 0x50, OSuppressed = 0x54, OWindow = 0x58, OMessage = 0x5c, OSelected = 0x60,
    OProcess = 0x64, OHandle = 0x68, OReceiveTimeout = 0x6c, OSendTimeout = 0x70, OUserEvent = 0x74,
    OEvent = 0x78, OErrors = 0x7c, OError = 0xa4, OCatalog = 0xa8, OGroup = 0xac, OGroupPriority = 0xb0,
    OOption3001 = 0xb4, OCreateFlags = 0xc4, ODeferred = 0xc8, OMagic = 0xe8, HeaderBytes = 0xec,
    OLocalAddress = 0x100, ORemoteAddress = 0x110, SlotBytes = 0x140,
};
constexpr U Magic = 0x786a786b;   // 'kxjx'
// +40h option bits (ioctl_10e ): 1 BROADCAST, 2 DEBUG, 4 DONTROUTE,
// 8 KEEPALIVE, 10h OOBINLINE, 20h REUSEADDR, 40h send shut down, 80h receive
// shut down, 100h blocking.
enum : U { Broadcast = 1, Debug = 2, DontRoute = 4, KeepAlive = 8, OobInline = 0x10, ReuseAddress = 0x20,
           SendShut = 0x40, ReceiveShut = 0x80, Blocking = 0x100 };
// +4Ch states: 1 open, 2 bound, 3 listening, 5 connecting, 6 connected,
// 7 connected and the peer closed, 9 closed, 0Bh accept deferred.
enum : U { Open = 1, Bound = 2, Listening = 3, Connecting = 5, Connected = 6, PeerClosed = 7, Closed = 9,
           NetDown = 0xa, Deferred = 0xb };
// Sockets of the whole session; each process has up to 255 handles of its
// own (src/x86_kernel_pipes.cpp).
constexpr U Slots = 512;
constexpr U ArenaBytes = Slots * SlotBytes;
// DNS endpoint for this network provider. RNR20 sends UDP port 53 queries
// here and receives answers from the host resolver.
constexpr U DnsServer = 0x0a000203;   // 10.0.2.3
constexpr U DnsPort = 53;

struct Datagram
{
    U from = 0, port = 0, bytes = 0;
    unsigned char data[512];
};
struct Native
{
    bool used = false;
    int host = -1;
    Application *owner = nullptr;
    // What the guest asked bind for; the host socket may sit on the
    // listening address of the policy instead (Listen).
    U bind_address = 0, bind_port = 0;
    // A datagram socket's connect lives here, as in AFVXD: the host socket
    // stays unconnected.
    U peer_address = 0, peer_port = 0;
    bool peer = false;
    // Answers of the DNS server of the network that wait to be read.
    Datagram *inbox = nullptr;
    U inbox_count = 0;
    int lookup = -1;           // host ticket of a DNS question in flight
    U connect_error = 0;       // a connect the host refused at once, reported by Poll
    unsigned char question[512];
    U question_bytes = 0, question_port = 0;
};
struct Waiter
{
    bool used = false;
    U slot = 0, mask = 0, procedure = 0, context = 0, id = 0;
    Application *app = nullptr;
    U thread = 0, thread_id = 0;
    // The request of an overlapped receive (kind 10h) or send (kind 20h),
    //  the OVERLAPPED, the WSABUF array, flags, the sender's
    // address and length, what is done and what is asked for.
    U kind = 0, overlapped = 0, buffers = 0, count = 0, flags = 0, from = 0, fromlen = 0, done = 0, total = 0;
    U to = 0, tolen = 0;
};
Native natives[Slots];
Waiter waiters[256];
} // namespace
} // namespace X86
// Saved states (src/x86_state_machine.cpp): a socket of the host is not part
// of one. The sockets of a loaded machine have lost theirs: the next call on
// one fails as on a network that went away, and a program that only opened a
// socket and never used it notices nothing.
unsigned MachineState::OpenSockets()
{
    unsigned open = 0;
    for (const auto &native : X86::natives)
        if (native.used && native.host >= 0)
            ++open;
    return open;
}
void MachineState::DropSockets()
{
    for (auto &native : X86::natives)
        if (native.used)
        {
            native.host = -1;
            native.lookup = -1;
        }
}
namespace X86
{
namespace
{
unsigned char *arena = nullptr;
U next_wait_id = 1;
bool post_routine = false;       // ioctl_113 was called

U Get(U slot, U offset)
{
    U v;
    memcpy(&v, arena + slot * SlotBytes + offset, 4);
    return v;
}
void Set(U slot, U offset, U value) { memcpy(arena + slot * SlotBytes + offset, &value, 4); }
unsigned char *Bytes(U slot, U offset) { return arena + slot * SlotBytes + offset; }
U Object(U slot) { return Win98SessionArena::SystemBase + slot * SlotBytes; }
bool Stream(U slot) { const U type = Get(slot, OType); return type != 2 && type != 3; }

U MapError(int error)
{
    switch (-error)
    {
    case NetHost::WouldBlock: case NetHost::InProgress: return Wsaewouldblock;
    case NetHost::Already: return 0x2735;   // WSAEALREADY
    case NetHost::NotConnected: return Wsaenotconn;
    case NetHost::IsConnected: return Wsaeisconn;
    case NetHost::ConnectionRefused: return Wsaeconnrefused;
    case NetHost::ConnectionReset: return Wsaeconnreset;
    case NetHost::ConnectionAborted: return Wsaeconnaborted;
    case NetHost::TimedOut: return Wsaetimedout;
    case NetHost::NetworkUnreachable: return Wsaenetunreach;
    case NetHost::HostUnreachable: return Wsaehostunreach;
    case NetHost::AddressInUse: return Wsaeaddrinuse;
    case NetHost::AddressNotAvailable: return Wsaeaddrnotavail;
    case NetHost::Access: return Wsaeacces;
    case NetHost::Invalid: return Wsaeinval;
    case NetHost::MessageSize: return Wsaemsgsize;
    case NetHost::NoBuffers: return Wsaenobufs;
    case NetHost::Shutdown: return Wsaeshutdown;
    case NetHost::NotSupported: return Wsaeopnotsupp;
    default: return Wsaenetdown;
    }
}
// sockaddr_in: family, port in network order, address in network order.
void PutAddress(unsigned char *out, U address, U port)
{
    memset(out, 0, 16);
    out[0] = 2;
    out[2] = (unsigned char)(port >> 8); out[3] = (unsigned char)port;
    out[4] = (unsigned char)(address >> 24); out[5] = (unsigned char)(address >> 16);
    out[6] = (unsigned char)(address >> 8); out[7] = (unsigned char)address;
}
void SetLocal(U slot, U address, U port)
{
    PutAddress(Bytes(slot, OLocalAddress), address, port);
    Set(slot, OLocal, Object(slot) + OLocalAddress);
    Set(slot, OLocalLength, 16);
}
void SetRemote(U slot, U address, U port)
{
    PutAddress(Bytes(slot, ORemoteAddress), address, port);
    Set(slot, ORemote, Object(slot) + ORemoteAddress);
    Set(slot, ORemoteLength, 16);
}
int SlotOf(U object)
{
    if (object < Win98SessionArena::SystemBase) return -1;
    const U offset = object - Win98SessionArena::SystemBase;
    if (offset % SlotBytes || offset / SlotBytes >= Slots) return -1;
    return (int)(offset / SlotBytes);
}
// The adapter the stack shows: the first interface of the host that is up and
// not the loopback. Its address is the guest's address.
struct Adapter
{
    bool found = false;
    NetHost::Interface loopback, primary;
};
Adapter Adapters(NetHost::Sockets &net)
{
    Adapter out;
    NetHost::Interface list[16];
    const U count = net.Interfaces(list, 16);
    out.loopback.address = 0x7f000001;
    out.loopback.mask = 0xff000000;
    out.loopback.loopback = true;
    // The interface of the host's default route first, so a machine with
    // several (a VPN beside the card) shows the one its traffic leaves by.
    for (U i = 0; i < count; ++i)
        if (!list[i].loopback && list[i].address && list[i].gateway && !out.found) { out.primary = list[i]; out.found = true; }
    for (U i = 0; i < count; ++i)
        if (!list[i].loopback && list[i].address && !out.found) { out.primary = list[i]; out.found = true; }
    return out;
}
U Net(U address) { return address >> 24 | (address >> 8 & 0xff00) | (address << 8 & 0xff0000) | address << 24; }
// The network card the registry names (src/neo98_hardware.h).
constexpr auto &AdapterDescription = Neo98Hardware::NetName;
bool released = false;   // VDHCP code 3 gave the lease back
// The route table of the stack (WsControl's IPRouteEntry, VIP code 69h): the
// loopback net and the adapter's net (direct, type 3), and a default route
// through the adapter (type 4, indirect) to the host's default gateway on
// that interface. Without one the entry carries the adapter's own address as
// next hop. Interface 1 is the loopback, 2 the adapter.
struct IpRoute { U destination, index, next, mask, kind; };
U IpRoutes(const Adapter &adapters, bool up, IpRoute (&routes)[3])
{
    routes[0] = {0x7f000000, 1, 0x7f000001, 0xff000000, 3};
    U count = 1;
    if (up)
    {
        const U a = adapters.primary.address, m = adapters.primary.mask;
        routes[count++] = {a & m, 2, a, m, 3};
        routes[count++] = {0, 2, adapters.primary.gateway ? adapters.primary.gateway : a, 0, 4};
    }
    return count;
}
// TDI status values as VTCP and VIP answer them in Windows 98.
enum : U { TdiSuccess = 0, TdiBufferOverflow = 9, TdiInvalidRequest = 0xfe, TdiInvalidQuery = 0x1f };
} // namespace

// The pieces of WSOCK2 that need the process: guest memory, the APC queue of
// a thread, the window of an async select.
struct Wsock2
{
    Application &app;
    NetHost::Sockets &net;

    // Validate the magic at +0E8h, a live list link, then the process joins
    // the socket's process list.
    U Validate(U object, int &slot)
    {
        slot = SlotOf(object);
        if (slot < 0 || !natives[slot].used || Get(slot, OMagic) != Magic || Get(slot, OLink) == object)
            return Wsaenotsock;
        Set(slot, OProcess, app.ProcessId());
        return 0;
    }
    bool Readable(U address, U bytes) { return !bytes || app.virtual_memory.Access(address, bytes, false); }
    bool Writable(U address, U bytes) { return !bytes || app.virtual_memory.Access(address, bytes, true); }

    //  for a Win32 wait: VWIN32_QueueUserApc(routine, status, thread).
    void Complete(Waiter &w, U status)
    {
        if (w.kind) { Overlapped(w, status); return; }
        if (w.app)
        {
            auto &t = w.app->threads[w.thread];
            if (t.fiber && !t.done && t.id == w.thread_id)
            {
                const U args[] = {status};
                w.app->EnqueueApc(w.thread, w.procedure, args, 1, 0);
            }
        }
        w = Waiter{};
    }
    //  a wait of the running thread; fires at once when the event is
    // already there.
    U Register(U slot, U mask, U procedure, U context)
    {
        for (auto &w : waiters)
        {
            if (w.used) continue;
            w.used = true;
            w.slot = slot; w.mask = mask; w.procedure = procedure; w.context = context;
            w.app = &app; w.thread = app.current_thread; w.thread_id = app.threads[app.current_thread].id;
            w.id = next_wait_id++;
            if (next_wait_id >= 0x80000000u) next_wait_id = 1;
            const U id = w.id;
            if ((Get(slot, OEvents) & ~Get(slot, OSuppressed)) & mask) Complete(w, 0);
            return id;
        }
        return 0;
    }
    //  with : an overlapped request moves on when its event
    // comes. Without an error the I/O is tried again; a request that still
    // cannot move keeps waiting, anything else completes.
    void Overlapped(Waiter &w, U status)
    {
        if (!w.app) { w = Waiter{}; return; }
        Wsock2 other{*w.app, net};
        U bytes = 0;
        if (w.kind == 0x30)
        {
            // A pending SIO_ADDRESS_LIST_CHANGE or SIO_ROUTING_INTERFACE_CHANGE
            //  it ends with the change or with the socket.
            if (status == Aborted) status = 0x3e3;
            Finish(*w.app, w.thread, w.thread_id, w.overlapped, w.procedure, w.context, status, 0);
            w = Waiter{};
            return;
        }
        if (!status && natives[w.slot].used)
        {
            if (w.kind == 0x10)
            {
                U fromlen = w.fromlen ? w.app->Read32(w.fromlen) : 0;
                status = other.Receive(w.slot, w.buffers, w.count, w.flags, w.from, fromlen, bytes);
                if (status == Wsaewouldblock) return;
                if (!status && w.fromlen) w.app->Write32(w.fromlen, fromlen);
                if (!status && bytes > w.total) status = Wsaemsgsize;
            }
            else
            {
                U sent = 0;
                status = other.Send(w.slot, w.buffers, w.count, w.done, w.flags, w.to, w.tolen, sent);
                if (!status) w.done += sent;
                if (status == Wsaewouldblock || (!status && w.done < w.total)) return;
                bytes = w.done;
            }
        }
        if (status == Aborted) status = 0x3e3;   // ERROR_OPERATION_ABORTED
        Finish(*w.app, w.thread, w.thread_id, w.overlapped, w.procedure, w.context, status, bytes);
        w = Waiter{};
    }
    // The end of an overlapped request: Internal and InternalHigh of the
    // OVERLAPPED, then the completion routine as APC or the event.
    static void Finish(Application &app, U thread, U thread_id, U overlapped, U procedure, U context, U status, U bytes)
    {
        if (app.virtual_memory.Access(overlapped, 12, true))
        {
            app.Write32(overlapped, status);
            app.Write32(overlapped + 4, bytes);
            app.Write32(overlapped + 8, 0);
        }
        if (procedure)
        {
            auto &t = app.threads[thread];
            if (t.fiber && !t.done && t.id == thread_id)
            {
                const U args[] = {context};
                app.EnqueueApc(thread, procedure, args, 1, 0);
            }
        }
        else if (context)
        {
            SetEvent(context);
            CloseEvent(context);
        }
    }
    U RegisterOverlapped(U slot, U mask, const Waiter &request)
    {
        for (auto &w : waiters)
        {
            if (w.used) continue;
            w = request;
            w.used = true;
            w.slot = slot; w.mask = mask;
            w.app = &app; w.thread = app.current_thread; w.thread_id = app.threads[app.current_thread].id;
            w.id = next_wait_id++;
            if (next_wait_id >= 0x80000000u) next_wait_id = 1;
            return w.id;
        }
        return 0;
    }
    //  every wait on the socket ends with `status`.
    static void CompleteAll(Wsock2 *self, U slot, U status)
    {
        for (auto &w : waiters)
            if (w.used && w.slot == slot && w.mask) self->Complete(w, status);
    }
    //  and the system-VM callback : the message reaches the
    // window's queue with wParam = the application's SOCKET (+68h) and
    // lParam = WSAMAKESELECTREPLY(event, error).
    void Post(U slot, U event, U error)
    {
        const U window = Get(slot, OWindow);
        if (!window || window == ~0u) return;
        User::PostMessage((User::HWND)window, Get(slot, OMessage), Get(slot, OHandle), (error << 16) | (event & 0xffff));
    }
    static void SetEvent(U ring0)
    {
        if ((ring0 & 0xff000000u) != 0x8e000000u) return;
        const U object = (ring0 & 0xffffffu) - 1;
        if (object < Application::KernelObjectSlots) Application::events[object].signaled = true;
    }
    static void ResetEvent(U ring0)
    {
        if ((ring0 & 0xff000000u) != 0x8e000000u) return;
        const U object = (ring0 & 0xffffffu) - 1;
        if (object < Application::KernelObjectSlots) Application::events[object].signaled = false;
    }
    // _VWIN32_CloseVxDHandle: the reference OpenVxDHandle added goes back.
    static void CloseEvent(U ring0)
    {
        if ((ring0 & 0xff000000u) != 0x8e000000u) return;
        const U object = (ring0 & 0xffffffu) - 1;
        if (object < Application::KernelObjectSlots && Application::events[object].references)
            --Application::events[object].references;
    }
    // svc3: the provider reports an event with its error.
    void Fire(U slot, U event, U error)
    {
        if (app.log)
        {
            char text[120];
            snprintf(text, sizeof text, "WSOCK2 event socket=%x event=%x error=%x", Get(slot, OHandle), event, error);
            app.log(text);
        }
        static const U order[] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200};
        int index = -1;
        for (int i = 0; i < 10; ++i)
            if (order[i] == event) index = i;
        if (event == FdOob && (Get(slot, OOptions) & OobInline)) { event = FdRead; index = -1; }
        if (index >= 0) Set(slot, OErrors + 4 * (U)index, error);
        Set(slot, OError, error);
        const U old = Get(slot, OEvents);
        if (event == FdConnect)
        {
            if (!error)
            {
                Set(slot, OSuppressed, Get(slot, OSuppressed) & ~FdWrite);
                Set(slot, OEvents, old | FdWrite);
            }
            else if (!Get(slot, OWindow) && !Get(slot, OEvent))
                event = ConnectFailed;
        }
        Set(slot, OEvents, Get(slot, OEvents) | event);
        event &= ~Get(slot, OSuppressed);
        for (auto &w : waiters)
            if (w.used && w.slot == slot && (w.mask & event)) Complete(w, error);
        if (!Get(slot, OWindow))
        {
            const U ring0 = Get(slot, OEvent);
            if (ring0 && (Get(slot, OSelected) & event) && !(old & event)) SetEvent(ring0);
            return;
        }
        if ((Get(slot, OSelected) & event) && !(old & event))
        {
            if (!error) Set(slot, OSuppressed, Get(slot, OSuppressed) | (event & (FdConnect | FdClose)));
            else if (event == FdConnect) Set(slot, OEvents, Get(slot, OEvents) & ~FdConnect);
            Post(slot, event, error);
        }
        //  a connect that succeeded is writable at once.
        if (event == FdConnect && !error && (~old & Get(slot, OSelected) & FdWrite)) Post(slot, FdWrite, 0);
    }

    // The provider side: what AFVXD and MSTCP did, on the host socket.
    U HostFailure(int error) { return MapError(error); }

    void CloseHost(U slot)
    {
        auto &n = natives[slot];
        if (n.host >= 0) net.Close(n.host);
        n.host = -1;
        delete[] n.inbox;
        n.inbox = nullptr;
        n.inbox_count = 0;
        n.lookup = -1;
    }
    //  with prov_2: the object leaves the list, the provider closes.
    void Destroy(U slot, bool hard)
    {
        auto &n = natives[slot];
        CompleteAll(this, slot, Aborted);
        for (auto &w : waiters)
            if (w.used && w.slot == slot) w = Waiter{};
        Set(slot, OSelected, 0);
        if (Get(slot, OEvent)) CloseEvent(Get(slot, OEvent));
        Set(slot, OEvent, 0);
        Set(slot, OState, Closed);
        Set(slot, OLink, Object(slot));
        if (n.host >= 0)
        {
            const U linger = Get(slot, OLinger);
            if (hard || ((linger & 0xffff) && !(linger >> 16))) net.SetOption(n.host, NetHost::Linger, 0);
        }
        CloseHost(slot);
        n = Native{};
    }

    // The policy of src/net_host.h: a listener sits on the listening address.
    // A stream socket keeps the port bind gave it and moves there when it
    // starts to listen, so getsockname between the two already knows the port.
    U Relisten(U slot)
    {
        auto &n = natives[slot];
        const U address = net.ListenAddress(n.bind_address);
        if (address == n.bind_address) return 0;
        U bound = 0, port = 0;
        if (net.Name(n.host, false, bound, port) < 0) return Wsaenetdown;
        const int fresh = net.Open(true);
        if (fresh < 0) return Wsaenobufs;
        if (Get(slot, OOptions) & ReuseAddress) net.SetOption(fresh, NetHost::ReuseAddress, 1);
        net.Close(n.host);
        const int r = net.Bind(fresh, address, port);
        n.host = fresh;
        if (r < 0) return HostFailure(r);
        return 0;
    }
    // prov_3: bind. A datagram socket on a fixed port is a server and gets
    // the listening address of the policy.
    U BindHost(U slot, U address, U port)
    {
        auto &n = natives[slot];
        U actual = address;
        if (!Stream(slot) && port) actual = net.ListenAddress(address);
        // A closed Win98 listener can bind its port again while an old TCP
        // connection is in TIME_WAIT (ReactOS WSAAsync's consecutive tests).
        // POSIX needs SO_REUSEADDR on both generations for that. Live guest
        // endpoint conflicts still follow OOptions in the check below.
        if (Stream(slot)) {
            const int error = net.SetOption(n.host, NetHost::ReuseAddress, 1);
            if (error < 0) return HostFailure(error);
        }
        // A datagram socket with SO_REUSEADDR shares its port with every
        // socket of the host that does the same: the members of a multicast
        // group or the broadcast listeners of other NEO98 sessions and of
        // other programs on the Mac, not only the sockets of this machine.
        if (!Stream(slot) && port && (Get(slot, OOptions) & ReuseAddress))
        {
            int error = net.SetOption(n.host, NetHost::ReuseAddress, 1);
            if (error >= 0) error = net.SetOption(n.host, NetHost::SharePort, 1);
            if (error < 0) return HostFailure(error);
        }
        // Win98 needs SO_REUSEADDR only on the new socket, even when the
        // existing endpoint is listening. POSIX requires cooperation from
        // both sockets. Keep that host setting separate from the guest bit.
        if (port) for (U other = 0; other < Slots; ++other)
        {
            auto &peer = natives[other];
            if (other == slot || !peer.used || peer.host < 0 || peer.bind_port != port ||
                Get(other, OType) != Get(slot, OType)) continue;
            const U peer_address = Stream(other) ? peer.bind_address : net.ListenAddress(peer.bind_address);
            if (actual && peer_address && actual != peer_address) continue;
            if (!(Get(slot, OOptions) & ReuseAddress)) return Wsaeaddrinuse;
            const int hosts[] = {peer.host, n.host};
            for (const int host : hosts)
            {
                int error = net.SetOption(host, NetHost::ReuseAddress, 1);
                if (error < 0) return HostFailure(error);
                error = net.SetOption(host, NetHost::SharePort, 1);
                if (error < 0) return HostFailure(error);
            }
        }
        const int r = net.Bind(n.host, actual, port);
        if (r < 0) return HostFailure(r);
        U bound = 0, real_port = port;
        if (net.Name(n.host, false, bound, real_port) < 0) real_port = port;
        n.bind_address = address;
        n.bind_port = real_port;
        SetLocal(slot, address, real_port);
        Set(slot, OState, Bound);
        return 0;
    }
    // IP_ADD_MEMBERSHIP and IP_DROP_MEMBERSHIP, which AFVXD hands to MSTCP
    // as a TDI set. A group's datagrams arrive at the port on every address,
    // so a wildcard socket the listening policy put on 127.0.0.1 moves to the
    // wildcard before it joins: a guest that joins a group takes part in the
    // network like on Windows 98.
    U Membership(U slot, bool join, U group, U local)
    {
        if (join)
            if (const U error = Widen(slot)) return error;
        const int r = net.Membership(natives[slot].host, join, group, local);
        return r < 0 ? HostFailure(r) : 0;
    }
    // A datagram socket the guest bound to the wildcard on a fixed port, which
    // the listening policy put on 127.0.0.1, moves to the wildcard: it joins
    // a group or talks to an address beyond the loopback. From 127.0.0.1 the
    // host can send nothing there and no answer could come back.
    U Widen(U slot)
    {
        auto &n = natives[slot];
        if (Stream(slot) || !n.bind_port || n.bind_address) return 0;
        U bound = 0, port = 0;
        if (net.Name(n.host, false, bound, port) < 0) return Wsaenetdown;
        if (!bound) return 0;
        const int fresh = net.Open(false);
        if (fresh < 0) return Wsaenobufs;
        net.SetOption(fresh, NetHost::ReuseAddress, 1);
        net.SetOption(fresh, NetHost::SharePort, 1);
        if (Get(slot, OOptions) & Broadcast) net.SetOption(fresh, NetHost::Broadcast, 1);
        net.Close(n.host);
        n.host = fresh;
        const int r = net.Bind(fresh, 0, port);
        return r < 0 ? HostFailure(r) : 0;
    }
    // The bookkeeping ioctl_101 does after a successful bind.
    void AfterBind(U slot)
    {
        if (!Stream(slot))
        {
            Set(slot, OSuppressed, 0);
            Fire(slot, FdWrite, 0);
        }
        else Set(slot, OSuppressed, Get(slot, OSuppressed) & ~FdConnect);
    }

    // One look at the host socket stands for the TDI indications AFVXD got:
    // connect completion, accept ready, receive, disconnect and send space.
    void Poll(U slot)
    {
        auto &n = natives[slot];
        if (!n.used || n.host < 0) return;
        const U state = Get(slot, OState);
        if (n.lookup >= 0) Resolve(slot);
        const U bits = net.Poll(n.host) | (n.inbox_count ? NetHost::Readable : 0);
        if (state == Connecting)
        {
            int error = 0;
            U failure = n.connect_error;
            n.connect_error = 0;
            if (!failure)
            {
                if (!(bits & (NetHost::Writable | NetHost::Failed | NetHost::HungUp))) return;
                net.GetOption(n.host, NetHost::PendingError, error);
                if (error) failure = MapError(-error);
            }
            if (!failure)
            {
                Set(slot, OState, Connected);
                U address = 0, port = 0;
                if (!net.Name(n.host, true, address, port)) SetRemote(slot, address, port);
                if (!net.Name(n.host, false, address, port)) SetLocal(slot, address, port);
                Fire(slot, FdConnect, 0);
                Set(slot, OSuppressed, FdAccept);
                Fire(slot, FdWrite, 0);
            }
            else
            {
                // A BSD socket whose connect failed cannot connect again; the
                // TDI address object of AFVXD could. A fresh host socket on the
                // same local address keeps state 2 usable.
                Set(slot, OState, Bound);
                const int fresh = net.Open(true);
                if (fresh >= 0)
                {
                    net.Close(n.host);
                    n.host = fresh;
                    if (n.bind_port) net.Bind(n.host, n.bind_address, n.bind_port);
                }
                Fire(slot, FdConnect, failure);
            }
            return;
        }
        if (state == Listening)
        {
            if ((bits & NetHost::Readable) && !(Get(slot, OEvents) & FdAccept)) Fire(slot, FdAccept, 0);
            return;
        }
        if (Stream(slot))
        {
            if (state != Connected && state != PeerClosed) return;
            if (bits & NetHost::Exception)
                if (!(Get(slot, OEvents) & FdOob)) Fire(slot, FdOob, 0);
            if (bits & NetHost::Readable)
            {
                const int pending = net.Pending(n.host);
                if (pending > 0)
                {
                    if (!(Get(slot, OEvents) & FdRead)) Fire(slot, FdRead, 0);
                }
                else if (state == Connected)
                {
                    char probe;
                    const int r = net.Receive(n.host, &probe, 1, NetHost::ReceivePeek, nullptr, nullptr);
                    if (r == 0 || (r < 0 && r != -NetHost::WouldBlock))
                    {
                        const bool reset = r < 0;
                        Set(slot, OState, PeerClosed);
                        if (reset)
                        {
                            Set(slot, OFlags, Get(slot, OFlags) | 1);
                            Fire(slot, FdClose, Wsaeconnreset);
                            CompleteAll(this, slot, Wsaeconnreset);
                        }
                        else Fire(slot, FdClose, 0);
                    }
                }
            }
            if (state == Connected && (bits & NetHost::Writable) && !(Get(slot, OEvents) & FdWrite))
                Fire(slot, FdWrite, 0);
            return;
        }
        if (state != Bound && state != Connected) return;
        if ((bits & NetHost::Readable) && !(Get(slot, OEvents) & FdRead)) Fire(slot, FdRead, 0);
        if ((bits & NetHost::Writable) && !(Get(slot, OEvents) & FdWrite)) Fire(slot, FdWrite, 0);
    }

    // The DNS server of the network: a question to 10.0.2.3:53 goes to the
    // resolver of the host, the answer comes back as a datagram from there.
    bool DnsQuestion(U slot, const unsigned char *data, U bytes)
    {
        auto &n = natives[slot];
        // A question while one is open, or one that is not a single question
        // with a name of labels, is lost like a datagram on the wire; RNR20
        // asks again after its timeout.
        if (bytes < 17 || bytes > sizeof n.question || n.lookup >= 0) return true;
        // Without the cable the question never reaches the server.
        if (!net.Online()) return true;
        if (data[4] != 0 || data[5] != 1) return true;
        char name[256];
        U at = 12, out = 0;
        while (at < bytes && data[at])
        {
            const U length = data[at++];
            if (length > 63 || at + length > bytes || out + length + 1 >= sizeof name) return true;
            if (out) name[out++] = '.';
            memcpy(name + out, data + at, length);
            out += length;
            at += length;
        }
        name[out] = 0;
        if (at + 5 > bytes) return true;
        const U type = (U)(data[at + 1] << 8 | data[at + 2]);
        memcpy(n.question, data, bytes);
        n.question_bytes = bytes;
        U reverse = 0;
        bool is_reverse = false;
        if (type == 12)
        {
            // d.c.b.a.in-addr.arpa
            U parts[4] = {}, count = 0, value = 0;
            const char *p = name;
            bool ok = true;
            for (; *p && count < 4; ++p)
            {
                if (*p >= '0' && *p <= '9') value = value * 10 + (U)(*p - '0');
                else if (*p == '.') { parts[count++] = value; value = 0; }
                else { ok = false; break; }
            }
            if (ok && count == 4 && !strcmp(p, "in-addr.arpa") && parts[0] < 256)
            {
                reverse = parts[3] << 24 | parts[2] << 16 | parts[1] << 8 | parts[0];
                is_reverse = true;
            }
        }
        if (type == 1) n.lookup = net.Query(name, 0);
        else if (is_reverse) n.lookup = net.Query(nullptr, reverse);
        else
        {
            // The host resolver answers addresses and names only.
            Reply(slot, nullptr, 0, nullptr, 4);   // NOTIMP
            return true;
        }
        if (n.lookup < 0) Reply(slot, nullptr, 0, nullptr, 2);   // SERVFAIL
        return true;
    }
    void Resolve(U slot)
    {
        auto &n = natives[slot];
        U addresses[16], count = 0;
        char text[256];
        const int r = net.Answer(n.lookup, addresses, 16, count, text, sizeof text);
        if (!r) return;
        n.lookup = -1;
        if (r < 0) Reply(slot, nullptr, 0, nullptr, 3);   // NXDOMAIN
        else Reply(slot, addresses, count, text, 0);
    }
    void Reply(U slot, const U *addresses, U count, const char *name, U rcode)
    {
        auto &n = natives[slot];
        if (!n.inbox) n.inbox = new Datagram[4];
        if (!n.inbox || n.inbox_count == 4) return;
        auto &d = n.inbox[n.inbox_count];
        const unsigned char *q = n.question;
        U at = 12;
        while (at < n.question_bytes && q[at]) at += q[at] + 1U;
        const U question_end = at + 5;
        const U type = (U)(q[at + 1] << 8 | q[at + 2]);
        memcpy(d.data, q, question_end);
        d.data[2] = (unsigned char)(0x80 | (q[2] & 0x79));   // QR, the opcode and RD of the question
        d.data[3] = (unsigned char)(0x80 | rcode);         // RA, RCODE
        d.data[6] = 0; d.data[7] = 0;                      // ANCOUNT
        d.data[8] = d.data[9] = d.data[10] = d.data[11] = 0;
        U out = question_end, answers = 0;
        if (!rcode && type == 1)
            for (U i = 0; i < count && out + 16 <= sizeof d.data; ++i, ++answers)
            {
                const unsigned char record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
                                                (unsigned char)(addresses[i] >> 24), (unsigned char)(addresses[i] >> 16),
                                                (unsigned char)(addresses[i] >> 8), (unsigned char)addresses[i]};
                memcpy(d.data + out, record, sizeof record);
                out += sizeof record;
            }
        else if (!rcode && name)
        {
            const unsigned char head[] = {0xc0, 0x0c, 0, 12, 0, 1, 0, 0, 0, 60, 0, 0};
            if (out + sizeof head + strlen(name) + 2 <= sizeof d.data)
            {
                memcpy(d.data + out, head, sizeof head);
                const U length_at = out + 10;
                out += sizeof head;
                const U start = out;
                const char *p = name;
                while (*p)
                {
                    const char *dot = strchr(p, '.');
                    const U length = dot ? (U)(dot - p) : (U)strlen(p);
                    if (!length || length > 63) break;
                    d.data[out++] = (unsigned char)length;
                    memcpy(d.data + out, p, length);
                    out += length;
                    p += length + (dot ? 1 : 0);
                }
                d.data[out++] = 0;
                d.data[length_at] = (unsigned char)((out - start) >> 8);
                d.data[length_at + 1] = (unsigned char)(out - start);
                answers = 1;
            }
        }
        d.data[6] = (unsigned char)(answers >> 8);
        d.data[7] = (unsigned char)answers;
        d.bytes = out;
        d.from = DnsServer;
        d.port = DnsPort;
        ++n.inbox_count;
    }

    // Gathers the WSABUF array of a request into one host buffer.
    bool Gather(U buffers, U count, U skip, unsigned char *out, U capacity, U &total)
    {
        total = 0;
        if (!Readable(buffers, count * 8)) return false;
        for (U i = 0; i < count; ++i)
        {
            U length = app.Read32(buffers + i * 8), base = app.Read32(buffers + i * 8 + 4);
            // The first `skip` bytes are already sent (or an
            // overlapped send that moved on in parts).
            const U drop = skip < length ? skip : length;
            base += drop; length -= drop; skip -= drop;
            if (total + length > capacity) length = capacity - total;
            if (length && (!Readable(base, length) || !app.Read(base, out + total, length))) return false;
            total += length;
        }
        return true;
    }
    bool Scatter(U buffers, U count, const unsigned char *data, U bytes)
    {
        for (U i = 0; i < count && bytes; ++i)
        {
            const U length = app.Read32(buffers + i * 8), base = app.Read32(buffers + i * 8 + 4);
            const U part = length < bytes ? length : bytes;
            if (part && (!Writable(base, part) || !app.Store(base, data, part))) return false;
            data += part;
            bytes -= part;
        }
        return true;
    }

    // prov_7: receive. Stream data comes from the host socket, datagrams one
    // at a time with their sender.
    U Receive(U slot, U buffers, U count, U flags, U from, U &fromlen, U &bytes)
    {
        auto &n = natives[slot];
        U total = 0;
        for (U i = 0; i < count; ++i) total += app.Read32(buffers + i * 8);
        const bool peek = (flags & 2) != 0, oob = (flags & 1) != 0;
        int host_flags = (peek ? NetHost::ReceivePeek : 0) | (oob ? NetHost::ReceiveOutOfBand : 0);
        if (Stream(slot))
        {
            static unsigned char buffer[65536];
            const U want = total < sizeof buffer ? total : sizeof buffer;
            const int r = net.Receive(n.host, buffer, want, host_flags, nullptr, nullptr);
            if (r < 0)
            {
                if (r == -NetHost::WouldBlock)
                {
                    // /: an empty queue clears the event.
                    Set(slot, OEvents, Get(slot, OEvents) & ~(oob ? FdOob : FdRead));
                    return Wsaewouldblock;
                }
                if (r == -NetHost::ConnectionReset) Set(slot, OFlags, Get(slot, OFlags) | 1);
                return HostFailure(r);
            }
            if (!Scatter(buffers, count, buffer, (U)r)) return Wsaefault;
            bytes = (U)r;
            if (!peek) Rearm(slot, oob ? FdOob : FdRead);
            return 0;
        }
        // Datagrams: first the answers of the DNS server, then the host.
        static unsigned char datagram[65536];
        U size = 0, sender = 0, port = 0;
        for (;;)
        {
            if (n.inbox_count)
            {
                auto &d = n.inbox[0];
                size = d.bytes; sender = d.from; port = d.port;
                memcpy(datagram, d.data, size);
                if (!peek)
                {
                    memmove(n.inbox, n.inbox + 1, (n.inbox_count - 1) * sizeof *n.inbox);
                    --n.inbox_count;
                }
                break;
            }
            const int r = net.Receive(n.host, datagram, sizeof datagram, host_flags, &sender, &port);
            if (r < 0)
            {
                if (r == -NetHost::WouldBlock)
                {
                    Set(slot, OEvents, Get(slot, OEvents) & ~FdRead);   //
                    return Wsaewouldblock;
                }
                return HostFailure(r);
            }
            size = (U)r;
            // A connected datagram socket only hears its peer.
            if (n.peer && (sender != n.peer_address || port != n.peer_port))
            {
                if (peek) net.Receive(n.host, datagram, sizeof datagram, 0, &sender, &port);
                continue;
            }
            break;
        }
        if (!Scatter(buffers, count, datagram, size < total ? size : total)) return Wsaefault;
        bytes = size;
        if (from)
        {
            //  copies the whole address without looking at fromlen.
            unsigned char address[16];
            PutAddress(address, sender, port);
            if (!Writable(from, 16) || !app.Store(from, address, 16)) return Wsaefault;
            fromlen = 16;
        }
        if (!peek) Rearm(slot, FdRead);
        return 0;
    }
    // After a receive AFVXD clears the event, or reports it again while data
    // is left and an async or event select listens.
    void Rearm(U slot, U event)
    {
        auto &n = natives[slot];
        int left = Stream(slot) ? net.Pending(n.host) : (int)n.inbox_count;
        if (!Stream(slot) && !left) left = (net.Poll(n.host) & NetHost::Readable) ? 1 : 0;
        Set(slot, OEvents, Get(slot, OEvents) & ~event);
        if (left > 0 && (Get(slot, OWindow) || Get(slot, OUserEvent))) Fire(slot, event, 0);
    }

    // prov_8: send. A stream socket sends what the host takes; a datagram
    // goes whole or not at all.
    U Send(U slot, U buffers, U count, U skip, U flags, U to, U tolen, U &bytes)
    {
        auto &n = natives[slot];
        static unsigned char buffer[65536];
        U total = 0;
        if (!Gather(buffers, count, skip, buffer, sizeof buffer, total)) return Wsaefault;
        const int host_flags = ((flags & 1) ? NetHost::SendOutOfBand : 0) | ((flags & 4) ? NetHost::SendDontRoute : 0);
        if (Stream(slot))
        {
            if (!total) { bytes = 0; return 0; }
            const int r = net.Send(n.host, buffer, total, host_flags);
            if (r < 0)
            {
                if (r == -NetHost::WouldBlock) { Set(slot, OEvents, Get(slot, OEvents) & ~FdWrite); return Wsaewouldblock; }
                if (r == -NetHost::ConnectionReset || r == -NetHost::Shutdown) Set(slot, OFlags, Get(slot, OFlags) | 1);
                return HostFailure(r);
            }
            bytes = (U)r;
            if ((U)r < total)
            {
                //  the buffer is full; a blocking caller waits for
                // FD_WRITE and sends the rest.
                Set(slot, OEvents, Get(slot, OEvents) & ~FdWrite);
                if (Get(slot, OOptions) & Blocking) return Wsaewouldblock;
            }
            return 0;
        }
        U address = n.peer_address, port = n.peer_port;
        if (to)
        {
            unsigned char sa[16];
            if (!Readable(to, 8) || !app.Read(to, sa, 8)) return Wsaefault;
            address = (U)sa[4] << 24 | (U)sa[5] << 16 | (U)sa[6] << 8 | sa[7];
            port = (U)sa[2] << 8 | sa[3];
        }
        if (Get(slot, OState) == Open)
        {
            // An unbound datagram socket binds on its first send (prov_8).
            const U error = BindHost(slot, 0, 0);
            if (error) return error;
            AfterBind(slot);
        }
        if (address == DnsServer && port == DnsPort)
        {
            DnsQuestion(slot, buffer, total);
            bytes = total;
            return 0;
        }
        if (address == 0xffffffffu && !(Get(slot, OOptions) & Broadcast)) return Wsaeacces;
        if ((address >> 24) != 127)
            if (const U error = Widen(slot)) return error;
        const int r = net.SendTo(n.host, buffer, total, host_flags, address, port);
        if (r < 0)
        {
            if (r == -NetHost::WouldBlock) { Set(slot, OEvents, Get(slot, OEvents) & ~FdWrite); return Wsaewouldblock; }
            return HostFailure(r);
        }
        bytes = (U)r;
        return 0;
    }
};

bool Application::Wsock2Available() const { return NetHost::Current() != nullptr; }

// The ioctls of WSOCK2. `a` are the eight arguments of
// DeviceIoControl; the block is lpvInBuffer and doubles as the output.
unsigned Application::Wsock2VxdControl(const unsigned *a)
{
    const unsigned result = Wsock2Dispatch(a);
    if (log && a[1] >= 0x100 && a[1] <= 0x11c)
    {
        char text[120];
        snprintf(text, sizeof text, "WSOCK2 ioctl %03x -> %x", a[1], result);
        log(text);
    }
    return result;
}
unsigned Application::Wsock2Dispatch(const unsigned *a)
{
    NetHost::Sockets *net = NetHost::Current();
    if (!net) return 1;
    const U code = a[1];
    if (code == ~0u) { Wsock2CloseDevice(); return 0; }   // DIOC_CLOSEHANDLE
    if (code == 0) return 0;                              // DIOC_OPEN
    if (code < 0x100 || code > 0x11c) return 1;           // ERROR_INVALID_FUNCTION
    const U block = a[2], size = a[3];
    if (block && !virtual_memory.Access(block, size, true)) return 0x6f8;   // ERROR_INVALID_USER_BUFFER
    if (!arena) arena = SystemArena("WSOCK2 sockets", Win98SessionArena::SystemBase, ArenaBytes);
    if (!arena) { Fail("WSOCK2: the system block for socket objects could not be mapped"); return Wsaenetdown; }
    Wsock2 w{*this, *net};
    auto in = [&](U offset) { return Read32(block + offset); };
    auto out = [&](U offset, U value) { Write32(block + offset, value); };
    int slot = -1;
    U error = 0;
    switch (code)
    {
    case 0x110:   // WSPSocket, ioctl_110
    {
        if (size < 0x20) return Wsaefault;
        const U family = in(0), type = in(4), flags = in(0x1c);
        U protocol = in(8);
        if (getenv("NEO98_TRACE_NET")) fprintf(stderr, "NET WSPSocket family=%u type=%u protocol=%u flags=%u\n", family, type, protocol, flags);
        // prov_0: the TCP/IP provider takes AF_INET streams and datagrams.
        if (family != 2) return Wsaeafnosupport;
        if (type == 1) { if (protocol && protocol != 6) return Wsaeprotonosupport; protocol = 6; }
        else if (type == 2) { if (protocol && protocol != 17) return Wsaeprotonosupport; protocol = 17; }
        else if (type == 3 && protocol == 1) {}   // raw ICMP, below
        else return Wsaesocktnosupport;   // other raw sockets need rights the host does not give
        if (type == 1 ? (flags & ~1u) : (flags & ~0x15u)) return Wsaeinval;
        for (U i = 0; i < Slots; ++i)
        {
            if (natives[i].used) continue;
            // A raw ICMP socket stands on the host's ICMP socket that needs no
            // rights: it sends the ICMP message without IP header and receives
            // whole IP datagrams, as a Win98 raw socket does.
            const int host = type == 3 ? net->OpenIcmp() : net->Open(type == 1);
            if (host < 0) return MapError(host);
            auto &n = natives[i];
            n = Native{};
            n.used = true;
            n.host = host;
            n.owner = this;
            memset(Bytes(i, 0), 0, SlotBytes);
            Set(i, OLink, Win98SessionArena::SystemBase + ArenaBytes);   // the list head, not the object itself
            Set(i, OWaiters, Object(i) + OWaiters);
            Set(i, OFamily, family); Set(i, OType, type); Set(i, OProtocol, protocol);
            Set(i, OSendBuffer, 0x2000); Set(i, OReceiveBuffer, 0x2000);
            Set(i, OHandle, in(0x10)); Set(i, OCatalog, in(0x14)); Set(i, OGroup, in(0x18));
            Set(i, OCreateFlags, flags);
            Set(i, OState, Open);
            Set(i, OOptions, Blocking);
            Set(i, OEvents, FdWrite);
            Set(i, OSuppressed, type != 1 ? 0x3fd : 0x3ff);
            Set(i, OReceiveTimeout, ~0u); Set(i, OSendTimeout, ~0u);
            Set(i, OProcess, ProcessId());
            Set(i, OMagic, Magic);
            out(0xc, Object(i));
            return 0;
        }
        return Wsaenobufs;
    }
    case 0x101:   // bind, ioctl_101
    case 0x103:   // connect, ioctl_103
    {
        if (size < 0x18) return Wsaefault;
        if ((error = w.Validate(in(4), slot))) return error;
        auto &n = natives[slot];
        const U state = Get(slot, OState), name = in(0), length = in(8);
        if (code == 0x101 && state != Open) return Wsaeinval;
        if (code == 0x103)
        {
            if (state == Connected && Stream(slot)) return Wsaeisconn;
            if (state != Open && state != Bound && state != Connected) return Wsaeinval;
            Set(slot, OEvents, Get(slot, OEvents) & ~(ConnectFailed | FdConnect));
        }
        unsigned char sa[16] = {};
        if ((int)length < 16 || !w.Readable(name, 16) || !Read(name, sa, 16)) return Wsaefault;
        if ((U)(sa[0] | sa[1] << 8) != Get(slot, OFamily)) return Wsaeafnosupport;
        const U address = (U)sa[4] << 24 | (U)sa[5] << 16 | (U)sa[6] << 8 | sa[7], port = (U)sa[2] << 8 | sa[3];
        if (code == 0x101)
        {
            if (Get(slot, OOptions) & ReuseAddress) net->SetOption(n.host, NetHost::ReuseAddress, 1);
            if ((error = w.BindHost(slot, address, port))) return error;
            w.AfterBind(slot);
            return 0;
        }
        if (state == Open)
        {
            // prov_5 binds an unbound socket to the wildcard first.
            if (!Stream(slot) && (error = w.BindHost(slot, 0, 0))) return error;
            if (Stream(slot)) Set(slot, OState, Bound);
            w.AfterBind(slot);
        }
        if (!Stream(slot))
        {
            // A datagram connect only names the peer; INADDR_ANY forgets it.
            n.peer = address != 0;
            n.peer_address = address;
            n.peer_port = port;
            // AFVXD's connect asks MSTCP for the route: a wildcard socket then
            // names the address of the route's interface (loopback for 127/8,
            // the card otherwise), as SIO_ROUTING_INTERFACE_QUERY does.
            U local = n.bind_address;
            if (n.peer && !local)
            {
                const Adapter adapters = Adapters(*net);
                if ((address >> 24) == 127) local = 0x7f000001;
                else if (adapters.found && !released) local = adapters.primary.address;
            }
            SetLocal(slot, local, n.bind_port);
            if (n.peer) { SetRemote(slot, address, port); Set(slot, OState, Connected); }
            else { Set(slot, ORemoteLength, 0); Set(slot, OState, Bound); }
            if (Get(slot, OWindow))
            {
                Set(slot, OSuppressed, Get(slot, OSuppressed) & ~FdWrite);
                w.Fire(slot, FdWrite, 0);
            }
            return 0;
        }
        //  state 5, FD_WRITE cleared, completion through FD_CONNECT.
        const int r = net->Connect(n.host, address, port);
        Set(slot, OState, Connecting);
        Set(slot, OEvents, Get(slot, OEvents) & ~FdWrite);
        SetRemote(slot, address, port);
        // TDI reports a failed connect through the completion as well, so an
        // error the host gives at once waits for the next poll.
        if (r < 0 && r != -NetHost::InProgress && r != -NetHost::WouldBlock) n.connect_error = MapError(r);
        if (!(Get(slot, OOptions) & Blocking)) return Wsaewouldblock;
        const U id = w.Register(slot, FdConnect | ConnectFailed, in(0xc), in(0x10));
        if (!id) return Wsaenobufs;
        out(0x14, id);
        return Pending;
    }
    case 0x108:   // listen, ioctl_108
    {
        if (size < 8) return Wsaefault;
        if ((error = w.Validate(in(0), slot))) return error;
        if (!Stream(slot)) return Wsaeopnotsupp;
        const U state = Get(slot, OState);
        if (state == Connected || state == Connecting) return Wsaeisconn;
        if (state == Listening) return 0;
        if (state != Bound) return Wsaeinval;
        if ((error = w.Relisten(slot))) return error;
        const int r = net->Listen(natives[slot].host, (int)in(4));
        if (r < 0) return MapError(r);
        Set(slot, OState, Listening);
        Set(slot, OBacklog, in(4));
        Set(slot, OSuppressed, 0x37);
        return 0;
    }
    case 0x100:   // accept, ioctl_100
    {
        if (size < 0x3c) return Wsaefault;
        if ((error = w.Validate(in(4), slot))) return error;
        if (!Stream(slot)) return Wsaeopnotsupp;
        if (Get(slot, OState) != Listening) return Wsaeinval;
        const U buffer = in(0), length = in(0xc);
        if (buffer && (int)Get(slot, OLocalLength) > (int)length) return Wsaefault;
        U address = 0, port = 0;
        const int host = net->Accept(natives[slot].host, address, port);
        if (host < 0)
        {
            if (host != -NetHost::WouldBlock) return MapError(host);
            Set(slot, OEvents, Get(slot, OEvents) & ~FdAccept);
            if (!(Get(slot, OOptions) & Blocking)) return Wsaewouldblock;
            const U id = w.Register(slot, FdAccept, in(0x14), in(0x18));
            if (!id) return Wsaenobufs;
            out(0x1c, id);
            return Pending;
        }
        int fresh = -1;
        for (U i = 0; i < Slots && fresh < 0; ++i)
            if (!natives[i].used) fresh = (int)i;
        if (fresh < 0) { net->Close(host); return Wsaenobufs; }
        const U s = (U)fresh;
        //  the new object starts as a copy of the listener.
        memcpy(Bytes(s, 0), Bytes(slot, 0), HeaderBytes);
        auto &n = natives[s];
        n = Native{};
        n.used = true; n.host = host; n.owner = this;
        Set(s, OEvents, 0); Set(s, OState, Connected); Set(s, OSuppressed, FdConnect);
        Set(s, OHandle, in(0x10));
        Set(s, OSendTimeout, ~0u); Set(s, OReceiveTimeout, ~0u);
        Set(s, OWaiters, Object(s) + OWaiters);
        Set(s, OProcess, ProcessId());
        U local = 0, local_port = 0;
        net->Name(host, false, local, local_port);
        SetLocal(s, local, local_port);
        SetRemote(s, address, port);
        if (Get(s, OEvent))
        {
            // The listener's event travels with the copy; MSAFD takes a
            // reference for it with OpenVxDHandle.
        }
        w.Fire(s, FdWrite, 0);
        Set(slot, OEvents, Get(slot, OEvents) & ~FdAccept);
        if (net->Poll(natives[slot].host) & NetHost::Readable) w.Fire(slot, FdAccept, 0);
        if (buffer && length >= 16)
        {
            if (!w.Writable(buffer, 16) || !Store(buffer, Bytes(s, ORemoteAddress), 16)) return Wsaefault;
            out(0xc, 16);
        }
        else out(0xc, 0);
        if (in(0x20))
        {
            out(0x2c, 16); out(0x34, 16);
            if (!w.Writable(in(0x28), 16) || !Store(in(0x28), Bytes(s, OLocalAddress), 16)) return Wsaefault;
            if (!w.Writable(in(0x30), 16) || !Store(in(0x30), Bytes(s, ORemoteAddress), 16)) return Wsaefault;
        }
        out(0x38, Get(s, OUserEvent));
        out(8, Object(s));
        return 0;
    }
    case 0x11c:   // accept condition result, ioctl_11c
    {
        if (size < 0x10) return Wsaefault;
        const int listener = SlotOf(in(0)), fresh = SlotOf(in(4));
        if (listener < 0 || fresh < 0) return Wsaenotsock;
        switch (in(8))
        {
        case 0: Set(listener, ODeferred, 0); Set(fresh, OState, Connected); Set(fresh, OGroup, in(0xc)); return 0;
        case 1: Set(listener, ODeferred, 0); Set(fresh, OState, Connected); return 0;
        case 2: Set(listener, ODeferred, Get(fresh, OHandle)); Set(fresh, OState, Deferred); return 0;
        default: return Wsaenotsock;
        }
    }
    case 0x102:   // close, ioctl_102
    {
        if (size < 4) return Wsaefault;
        if ((error = w.Validate(in(0), slot))) return error;
        const U linger = Get(slot, OLinger);
        if (!(Get(slot, OOptions) & Blocking) && (linger & 0xffff) && (linger >> 16)) return Wsaewouldblock;
        if (Get(slot, OState) == Closed) return 0;
        w.Destroy((U)slot, false);
        return 0;
    }
    case 0x10f:   // shutdown, ioctl_10f
    {
        if (size < 8) return Wsaefault;
        if ((error = w.Validate(in(0), slot))) return error;
        const U state = Get(slot, OState), how = in(4);
        if (Stream(slot) && state != Connected && state != PeerClosed) return Wsaenotconn;
        if (how > 2) return Wsaeinval;
        Set(slot, OOptions, Get(slot, OOptions) | (how == 0 ? ReceiveShut : how == 1 ? SendShut : ReceiveShut | SendShut));
        if (Stream(slot))
        {
            const int r = net->Shutdown(natives[slot].host, (int)how);
            if (r < 0 && r != -NetHost::NotConnected) return MapError(r);
        }
        return 0;
    }
    case 0x104:   // getpeername region
    case 0x105:   // getsockname
    {
        if (size < 0xc) return Wsaefault;
        if ((error = w.Validate(in(4), slot))) return error;
        const U state = Get(slot, OState), length = in(8), buffer = in(0);
        const bool peer = code == 0x104;
        if (peer && state != Connected && state != PeerClosed) return Wsaenotconn;
        if (!peer && state == Open) return Wsaeinval;
        const U have = Get(slot, peer ? ORemoteLength : OLocalLength);
        if ((int)have > (int)length) return Wsaefault;
        if (have && (!w.Writable(buffer, have) || !Store(buffer, Bytes(slot, peer ? ORemoteAddress : OLocalAddress), have)))
            return Wsaefault;
        out(8, have);
        return 0;
    }
    case 0x11b:   // header copy, ioctl_11b, no validation
    {
        if (size < 0xc) return Wsaefault;
        slot = SlotOf(in(0));
        const U buffer = in(4), length = in(8) < SlotBytes ? in(8) : SlotBytes;
        if (slot < 0 || !w.Writable(buffer, length) || !Store(buffer, Bytes(slot, 0), length)) return Wsaenobufs;
        return 0;
    }
    case 0x109:   // recv, ioctl_109
    {
        if (size < 0x34) return Wsaefault;
        if ((error = w.Validate(in(0xc), slot))) return error;
        const U state = Get(slot, OState), flags = in(0x18), count = in(0x10), buffers = in(0);
        if (Get(slot, OOptions) & ReceiveShut) return Wsaeshutdown;
        if (Stream(slot))
        {
            if (Get(slot, OFlags) & 1) return Wsaeconnreset;
            if (state == Connecting && !(Get(slot, OOptions) & Blocking)) return Wsaewouldblock;
            if (state != Connected && state != PeerClosed) return Wsaenotconn;
            if ((Get(slot, OOptions) & OobInline) && (flags & 1)) return Wsaeinval;
        }
        else
        {
            if (state != Bound && state != Connected) return Wsaeinval;
            if (flags & 1) return Wsaeopnotsupp;
        }
        if (!w.Readable(buffers, count * 8)) return Wsaefault;
        U total = 0;
        for (U i = 0; i < count; ++i)
        {
            const U length = Read32(buffers + i * 8);
            if (length & 0xf0000000u) return Wsaenobufs;
            total += length;
        }
        out(0x1c, 0);
        if (!total && Stream(slot)) return 0;
        U fromlen = in(0x14), bytes = 0;
        if (const U overlapped = in(0x30))
        {
            //  the overlapped path. Internal is STATUS_PENDING until
            // the request ends; an event of the caller starts cleared.
            if (!w.Writable(overlapped, 0x14)) return Wsaenobufs;
            Write32(overlapped + 4, 0); Write32(overlapped + 8, 0);
            if (!in(0x20) && in(0x24)) Wsock2::ResetEvent(in(0x24));
            Write32(overlapped, 0x103);
            error = w.Receive((U)slot, buffers, count, flags, in(4), fromlen, bytes);
            if (error == Wsaewouldblock)
            {
                Waiter request;
                request.kind = 0x10; request.overlapped = overlapped; request.buffers = buffers; request.count = count;
                request.flags = flags; request.from = in(4); request.fromlen = in(8); request.total = total;
                request.procedure = in(0x20); request.context = in(0x24);
                if (!w.RegisterOverlapped(slot, (flags & 1) ? FdOob | FdClose : FdRead | FdClose, request))
                    return Wsaenobufs;
                return 0x3e5;   // ERROR_IO_PENDING
            }
            if (error) return error;
            out(0x14, fromlen);
            if (in(8) && w.Writable(in(8), 4)) Write32(in(8), fromlen);
            out(0x1c, bytes);
            Wsock2::Finish(*this, current_thread, threads[current_thread].id, overlapped, in(0x20), in(0x24),
                           bytes > total ? Wsaemsgsize : 0, bytes);
            return 0;
        }
        error = w.Receive((U)slot, buffers, count, flags, in(4), fromlen, bytes);
        out(0x14, fromlen);
        if (error == Wsaewouldblock && (Get(slot, OOptions) & Blocking))
        {
            const U id = w.Register(slot, (flags & 1) ? FdOob | FdClose : FdRead | FdClose, in(0x20), in(0x24));
            if (!id) return Wsaenobufs;
            out(0x2c, id);
            out(0x28, Get(slot, OReceiveTimeout));
            return Pending;
        }
        if (error) return error;
        out(0x1c, bytes);
        return bytes > total ? Wsaemsgsize : 0;
    }
    case 0x10d:   // send, ioctl_10d
    {
        if (size < 0x34) return Wsaefault;
        if ((error = w.Validate(in(8), slot))) return error;
        const U state = Get(slot, OState), flags = in(0x14), to = in(4), tolen = in(0x18), count = in(0xc);
        if (Get(slot, OOptions) & SendShut) return Wsaeshutdown;
        if (Stream(slot))
        {
            if (Get(slot, OFlags) & 1) return Wsaeconnreset;
            if (state == Connecting && !(Get(slot, OOptions) & Blocking)) return Wsaewouldblock;
            if (state != Connected && state != PeerClosed) return Wsaenotconn;
        }
        else
        {
            if (to)
            {
                if (tolen < 2) return Wsaefault;
                if (!w.Readable(to, tolen < 16 ? tolen : 16)) return Wsaenobufs;
            }
            if (state != Connected && !to) return Wsaenotconn;
            if (state == Connected && to) return Wsaeisconn;
            if (flags & 1) return Wsaeopnotsupp;
            if (to && (U)(Read32(to) & 0xffff) != Get(slot, OFamily)) return Wsaeafnosupport;
        }
        if (!w.Readable(in(0), count * 8)) return Wsaefault;
        for (U i = 0; i < count; ++i)
            if (Read32(in(0) + i * 8) & 0xf0000000u) return Wsaenobufs;
        out(0x1c, 0);
        U bytes = 0;
        if (const U overlapped = in(0x30))
        {
            // The overlapped send: all bytes go, in parts if the host takes
            // them in parts, and the request ends when the last one went.
            if (!w.Writable(overlapped, 0x14)) return Wsaenobufs;
            U total = 0;
            for (U i = 0; i < count; ++i) total += Read32(in(0) + i * 8);
            Write32(overlapped + 4, 0); Write32(overlapped + 8, 0);
            if (!in(0x20) && in(0x24)) Wsock2::ResetEvent(in(0x24));
            Write32(overlapped, 0x103);
            error = w.Send((U)slot, in(0), count, 0, flags, to, tolen, bytes);
            if (error && error != Wsaewouldblock) return error;
            if (error == Wsaewouldblock) bytes = 0;
            if (bytes < total)
            {
                Waiter request;
                request.kind = 0x20; request.overlapped = overlapped; request.buffers = in(0); request.count = count;
                request.flags = flags; request.to = to; request.tolen = tolen; request.done = bytes; request.total = total;
                request.procedure = in(0x20); request.context = in(0x24);
                if (!w.RegisterOverlapped(slot, FdWrite, request)) return Wsaenobufs;
                return 0x3e5;
            }
            out(0x1c, bytes);
            Wsock2::Finish(*this, current_thread, threads[current_thread].id, overlapped, in(0x20), in(0x24), 0, bytes);
            return 0;
        }
        error = w.Send((U)slot, in(0), count, in(0x10), flags, to, tolen, bytes);
        out(0x1c, bytes);
        if (error == Wsaewouldblock && (Get(slot, OOptions) & Blocking))
        {
            const U id = w.Register(slot, FdWrite, in(0x20), in(0x24));
            if (!id) return Wsaenobufs;
            out(0x2c, id);
            out(0x28, Get(slot, OSendTimeout));
            return Pending;
        }
        return error;
    }
    case 0x10a:   // select, arm the waits, ioctl_10a
    case 0x10b:   // select, collect, ioctl_10b
    {
        if (size < 0x18) return Wsaefault;
        for (U set = 0; set < 3; ++set)
        {
            const U entries = in(set * 4), count = in(0xc + set * 4) & 0xffff;
            if (!count) continue;
            if (!w.Writable(entries, count * 16)) return Wsaefault;
            for (U i = 0; i < count; ++i)
            {
                const U entry = entries + i * 16, object = Read32(entry), mask = Read32(entry + 4);
                if (code == 0x10a)
                {
                    if (!object || w.Validate(object, slot)) return Wsaenotsock;
                    const U state = Get(slot, OState);
                    if (state == 8 || state == Closed) return Wsaenotsock;
                    if (mask & FdClose) Set(slot, OSuppressed, Get(slot, OSuppressed) & ~FdClose);
                    w.Poll((U)slot);
                    const U id = w.Register(slot, mask, in(0x18), in(0x1c));
                    if (!id) return Wsaenobufs;
                    Write32(entry + 0xc, id);
                    if (log)
                    {
                        char text[160];
                        snprintf(text, sizeof text, "WSOCK2 select arm socket=%x mask=%x events=%x suppressed=%x id=%u thread=%u",
                                 Get(slot, OHandle), mask, Get(slot, OEvents), Get(slot, OSuppressed), id, current_thread);
                        log(text);
                    }
                    continue;
                }
                if (!object || w.Validate(object, slot)) continue;
                w.Poll((U)slot);
                if (!((Get(slot, OEvents) & ~Get(slot, OSuppressed)) & mask)) Write32(entry, 0);
                for (auto &wt : waiters)
                    if (wt.used && wt.slot == (U)slot && wt.mask == mask && wt.app == this &&
                        wt.thread == current_thread)
                        wt = Waiter{};
            }
        }
        return 0;
    }
    case 0x10c:   // WSAAsyncSelect, ioctl_10c
    {
        if (size < 0x10) return Wsaefault;
        if (!post_routine) return Wsanotinitialised;
        slot = SlotOf(in(0));
        if (slot < 0 || !natives[slot].used) return Wsaenotsock;
        const U events = in(0xc), state = Get(slot, OState);
        Set(slot, OOptions, Get(slot, OOptions) & ~Blocking);
        if (!events)
        {
            Set(slot, OWindow, 0); Set(slot, OMessage, 0); Set(slot, OSelected, 0);
            return 0;
        }
        U mask = Get(slot, OSuppressed);
        if (state == Bound && Stream(slot)) mask |= FdWrite;
        if (state == Listening) mask |= 0x37;
        Set(slot, OSelected, events); Set(slot, OSuppressed, mask); Set(slot, OMessage, in(8)); Set(slot, OWindow, in(4));
        out(0xc, 0);
        w.Poll((U)slot);
        for (U bit = 1; bit <= 0x200; bit <<= 1)
            if (events & Get(slot, OEvents) & ~Get(slot, OSuppressed) & bit) w.Post((U)slot, bit, 0);
        return 0;
    }
    case 0x118:   // WSAEventSelect, ioctl_118
    {
        if (size < 0x10) return Wsaefault;
        if ((error = w.Validate(in(0), slot))) { Wsock2::CloseEvent(in(4)); return error; }
        const U events = in(0xc), state = Get(slot, OState);
        if (!events)
        {
            if (Get(slot, OEvent)) Wsock2::CloseEvent(Get(slot, OEvent));
            Set(slot, OSelected, 0); Set(slot, OEvent, 0); Set(slot, OUserEvent, 0);
            return 0;
        }
        U mask = Get(slot, OSuppressed);
        if (state == Bound && Stream(slot)) mask |= FdWrite;
        if (state == Listening) mask |= 0x37;
        Set(slot, OOptions, Get(slot, OOptions) & ~Blocking);
        Set(slot, OSelected, events); Set(slot, OSuppressed, mask);
        if (Get(slot, OEvent)) Wsock2::CloseEvent(Get(slot, OEvent));
        Set(slot, OEvent, in(4)); Set(slot, OUserEvent, in(8));
        Set(slot, OWindow, 0); Set(slot, OMessage, 0);
        w.Poll((U)slot);
        if ((Get(slot, OEvents) & ~mask) & events) Wsock2::SetEvent(in(4));
        return 0;
    }
    case 0x119:   // WSAEnumNetworkEvents, ioctl_119
    {
        if (size < 0x34) return Wsaefault;
        const U ring0 = in(4);
        if ((error = w.Validate(in(0), slot))) { Wsock2::CloseEvent(ring0); return error; }
        w.Poll((U)slot);
        const U selected = Get(slot, OSelected), suppressed = Get(slot, OSuppressed);
        const U report = Get(slot, OEvents) & selected & ~suppressed;
        Set(slot, OEvents, Get(slot, OEvents) & (~selected | suppressed));
        out(8, report);
        for (U i = 0; i < 10; ++i)
            if (report & (1u << i)) out(0xc + 4 * i, Get(slot, OErrors + 4 * i));
        if (ring0) { Wsock2::ResetEvent(ring0); Wsock2::CloseEvent(ring0); }
        return 0;
    }
    case 0x106:   // getsockopt, ioctl_106
    case 0x10e:   // setsockopt, ioctl_10e
    {
        if (size < 0x18) return Wsaefault;
        if ((error = w.Validate(in(4), slot))) return error;
        auto &n = natives[slot];
        const U level = in(8), name = in(0xc), value = in(0x14);
        const bool set = code == 0x10e, stream = Stream(slot);
        //  options that do not fit the socket type.
        if (level == 0xffff)
        {
            if (!stream && (name == 2 || name == 8 || name == 0x80 || name == 0x100 || name == 0xff7f || name == 0xffffff7f))
                return Wsaenoprotoopt;
            if (stream && name == 0x20) return Wsaenoprotoopt;
        }
        struct Bit { U name, bit; NetHost::Option host; };
        static const Bit bits[] = {{1, Debug, NetHost::Option(0)}, {4, ReuseAddress, NetHost::ReuseAddress},
                                   {8, KeepAlive, NetHost::KeepAlive}, {0x10, DontRoute, NetHost::DontRoute},
                                   {0x20, Broadcast, NetHost::Broadcast}, {0x100, OobInline, NetHost::OutOfBandInline}};
        if (!set)
        {
            U answer = 0;
            bool plain = true;
            if (level == 0xffff)
            {
                bool known = true;
                switch (name)
                {
                case 2: answer = Get(slot, OState) == Listening; break;
                case 0x1001: answer = Get(slot, OSendBuffer); break;
                case 0x1002: answer = Get(slot, OReceiveBuffer); break;
                case 0x1005: answer = Get(slot, OSendTimeout); break;
                case 0x1006: answer = Get(slot, OReceiveTimeout); break;
                case 0x1007: answer = Get(slot, OError); break;
                case 0x1008: answer = Get(slot, OType); break;
                case 0x2001: answer = Get(slot, OGroup); break;
                case 0x2002: answer = Get(slot, OGroupPriority); break;
                // Measured on original Win98 SE: streams report unlimited;
                // UDP reports 65535 minus its eight-byte header.
                case 0x2003:
                    if (stream) answer = ~0u;
                    else if (Get(slot, OType) == 2) answer = 65527;
                    else return Wsaenoprotoopt;
                    break;
                case 0x3001: answer = Get(slot, OOption3001); break;
                case 0xff7f: case 0xffffff7f: answer = !(Get(slot, OLinger) & 0xffff); break;
                case 0x80:
                {
                    if ((int)in(0x10) < 4) return Wsaefault;
                    if (!w.Writable(in(0), 4)) return Wsaefault;
                    Write32(in(0), Get(slot, OLinger));
                    out(0x10, 4);
                    return 0;
                }
                default:
                    known = false;
                    for (const auto &b : bits)
                        if (b.name == name) { answer = (Get(slot, OOptions) & b.bit) != 0; known = true; }
                }
                if (!known && name == 0x2005)
                {
                    // SO_PROTOCOL_INFOW: one of three WSAPROTOCOL_INFOW
                    // of 274h bytes that WSOCK2.VXD carries (TCP, UDP, raw at
                    // object 0 + 452Ch, chosen by the socket type), with the
                    // socket's catalog entry at +24h. They are read out of the
                    // installed WSOCK2.VXD, like the CSP keys out of ADVAPI32.
                    if (in(0x10) < 0x274) return Wsaefault;
                    if (!w.Writable(in(0), 0x274)) return Wsaefault;
                    const U type = Get(slot, OType), index = type == 2 ? 1 : type == 3 ? 2 : 0;
                    unsigned char info[0x274];
                    if (!Wsock2ProtocolTemplate(index, info))
                    {
                        Fail("WSOCK2: SO_PROTOCOL_INFO needs C:\\WINDOWS\\SYSTEM\\WSOCK2.VXD of the tree");
                        return Wsaenetdown;
                    }
                    const U catalog = Get(slot, OCatalog);
                    memcpy(info + 0x24, &catalog, 4);
                    Store(in(0), info, sizeof info);
                    out(0x10, 0x274);
                    return 0;
                }
                if (!known) return Wsaenoprotoopt;
            }
            else if (level == 6 && name == 1 && stream)
            {
                int v = 0;
                if (net->GetOption(n.host, NetHost::NoDelay, v) < 0) return Wsaenoprotoopt;
                answer = v != 0;
            }
            else if (level == 0 && !stream && name >= 9 && name <= 11)
            {
                // IP_MULTICAST_IF, IP_MULTICAST_TTL, IP_MULTICAST_LOOP
                int v = 0;
                if (net->GetOption(n.host, name == 9 ? NetHost::MulticastInterface
                                       : name == 10 ? NetHost::MulticastTtl : NetHost::MulticastLoop, v) < 0)
                    return Wsaenoprotoopt;
                answer = name == 9 ? __builtin_bswap32((U)v) : (U)v;
            }
            else return Wsaenoprotoopt;   // prov_10: IP options the host provider does not pass on
            if (plain)
            {
                out(0, 0); out(0x10, 0); out(0x14, answer);
            }
            return 0;
        }
        if (!w.Readable(in(0), 4)) return Wsaefault;
        if (level == 0xffff)
        {
            for (const auto &b : bits)
                if (b.name == name)
                {
                    Set(slot, OOptions, value ? Get(slot, OOptions) | b.bit : Get(slot, OOptions) & ~b.bit);
                    if (b.host) net->SetOption(n.host, b.host, value != 0);
                    return 0;
                }
            switch (name)
            {
            case 0x80:
                if (in(0x10) < 4) return Wsaefault;
                Set(slot, OLinger, Read32(in(0)));
                return 0;
            case 0xff7f: case 0xffffff7f:
                Set(slot, OLinger, (Get(slot, OLinger) & 0xffff0000u) | (value == 0));
                return 0;
            case 0x1005: Set(slot, OSendTimeout, value); return 0;
            case 0x1006: Set(slot, OReceiveTimeout, value); return 0;
            case 0x1007: Set(slot, OError, value); return 0;
            case 0x2002: Set(slot, OGroupPriority, value); return 0;
            case 0x3001: Set(slot, OOption3001, value); return 0;
            case 0x1001: case 0x1002:
            {
                const int r = net->SetOption(n.host, name == 0x1001 ? NetHost::SendBuffer : NetHost::ReceiveBuffer, (int)value);
                if (r < 0) return Wsaeinval;
                Set(slot, name == 0x1001 ? OSendBuffer : OReceiveBuffer, value);
                return 0;
            }
            default: return Wsaenoprotoopt;
            }
        }
        if (level == 6 && name == 1 && stream)
        {
            if (net->SetOption(n.host, NetHost::NoDelay, value != 0) < 0) return Wsaenoprotoopt;
            return 0;
        }
        // IPPROTO_IP: the multicast options of Winsock 2 (WSOCK32 turns the
        // Winsock 1.1 numbers 2..6 into these before MSAFD sees them).
        if (level == 0 && !stream)
        {
            int r = 0;
            switch (name)
            {
            case 9: r = net->SetOption(n.host, NetHost::MulticastInterface, (int)__builtin_bswap32(value)); break;
            case 10: r = net->SetOption(n.host, NetHost::MulticastTtl, (int)(value & 0xff)); break;
            case 11: r = net->SetOption(n.host, NetHost::MulticastLoop, value != 0); break;
            case 12: case 13:
                if (in(0x10) < 8 || !w.Readable(in(0), 8)) return Wsaefault;
                return w.Membership(slot, name == 12, __builtin_bswap32(Read32(in(0))),
                                    __builtin_bswap32(Read32(in(0) + 4)));
            default: return Wsaenoprotoopt;
            }
            return r < 0 ? w.HostFailure(r) : 0;
        }
        return Wsaenoprotoopt;
    }
    case 0x107:   // ioctlsocket and WSAIoctl, ioctl_107
    {
        if (size < 0x30) return Wsaefault;
        if ((error = w.Validate(in(0), slot))) return error;
        const U command = in(4), overlapped = in(0x2c);
        auto &n = natives[slot];
        //  with an OVERLAPPED, Internal is STATUS_PENDING and
        // InternalHigh and Offset are zero while the request runs, and the
        // caller's event starts cleared when there is no completion routine.
        if (overlapped)
        {
            if (!w.Writable(overlapped, 0x14)) return Wsaenobufs;
            Write32(overlapped + 4, 0); Write32(overlapped + 8, 0);
            if (!in(0x20) && in(0x24)) Wsock2::ResetEvent(in(0x24));
            Write32(overlapped, 0x103);
        }
        U bytes = 0;
        // The provider's part, prov_12 of AFVXD; FIONBIO stays in
        // WSOCK2.
        auto provider = [&]() -> U {
            if (command == 0x8004667e)   // FIONBIO
            {
                if (in(0x14) < 4) return Wsaefault;
                if (!in(8))
                {
                    if (Get(slot, OWindow) || Get(slot, OUserEvent)) return Wsaeinval;
                    Set(slot, OWindow, 0); Set(slot, OSelected, 0);
                    Set(slot, OOptions, Get(slot, OOptions) | Blocking);
                }
                else Set(slot, OOptions, Get(slot, OOptions) & ~Blocking);
                return 0;
            }
            const U source = in(0xc), source_length = in(0x14), target = in(0x10), capacity = in(0x18);
            //  the provider reports the bytes it needs; a smaller
            // buffer is WSAEFAULT.
            auto needs = [&](U needed) -> U { bytes = needed; return capacity < needed ? Wsaefault : 0; };
            switch (command)
            {
            case 0x4004667f:   // FIONREAD
            case 0x40047307:   // SIOCATMARK
            {
                if (capacity < 4 || !w.Writable(target, 4)) return Wsaefault;
                U answer = 0;
                if (command == 0x4004667f)
                {
                    if (Stream(slot)) { const int r = net->Pending(n.host); answer = r > 0 ? (U)r : 0; }
                    else if (n.inbox_count) answer = n.inbox[0].bytes;
                    else
                    {
                        // The size of the next datagram, as AFVXD  reports it.
                        static unsigned char probe[65536];
                        U from = 0, port = 0;
                        const int r = net->Receive(n.host, probe, sizeof probe, NetHost::ReceivePeek, &from, &port);
                        answer = r > 0 ? (U)r : 0;
                    }
                }
                else
                {
                    if (!Stream(slot) || !(Get(slot, OOptions) & OobInline)) return Wsaeinval;
                    const int r = net->AtMark(n.host);
                    answer = r > 0;
                }
                Write32(target, answer);
                bytes = 4;
                return 0;
            }
            case 0x88000009:   // SIO_MULTIPOINT_LOOPBACK: TDI set 0Bh
            case 0x8800000a:   // SIO_MULTICAST_SCOPE: TDI set 0Ah
            {
                if (source_length < 4 || !w.Readable(source, 4)) return Wsaefault;
                const U value = Read32(source);
                net->SetOption(n.host, command == 0x88000009 ? NetHost::MulticastLoop : NetHost::MulticastTtl,
                               command == 0x88000009 ? (value != 0) : (int)(value & 0xff));
                return 0;
            }
            case 0x8800000b: case 0x8800000c: case 0xc8000007: case 0xc8000008:
                return Wsaeopnotsupp;   // the QOS requests
            case 0x48000016:   // SIO_ADDRESS_LIST_QUERY
            {
                // The IP address table without 127.0.0.1, 0 and FFFFFFFF: the
                // count, then {sockaddr pointer, 10h} per address, the
                // sockaddr_in themselves from the end of the buffer down.
                U list[4], count = 0;
                const Adapter adapters = Adapters(*net);
                if (adapters.found && !released) list[count++] = adapters.primary.address;
                U needed = 4, written = 0, end = target + (capacity & ~3u);
                for (U i = 0; i < count; ++i)
                {
                    needed += 0x18;
                    if (capacity < needed) continue;
                    if (!w.Writable(target, needed)) return Wsaefault;
                    end -= 0x10;
                    const unsigned char address[16] = {2, 0, 0, 0, (unsigned char)(list[i] >> 24),
                                                       (unsigned char)(list[i] >> 16), (unsigned char)(list[i] >> 8),
                                                       (unsigned char)list[i]};
                    Store(end, address, 16);
                    Write32(target + 4 + written * 8, end);
                    Write32(target + 8 + written * 8, 0x10);
                    ++written;
                }
                if (capacity >= needed && w.Writable(target, 4)) Write32(target, written);
                return needs(needed);
            }
            case 0xc8000014:   // SIO_ROUTING_INTERFACE_QUERY
            {
                if (source_length < 0x10 || capacity < 0x10 || !source || !target) return Wsaefault;
                if (!w.Readable(source, 0x10) || !w.Writable(target, 0x10)) return Wsaefault;
                unsigned char to[16];
                Read(source, to, 16);
                if (to[0] != 2 || to[1] != 0) return Wsaeafnosupport;
                const U destination = (U)to[4] << 24 | to[5] << 16 | to[6] << 8 | to[7];
                // The route's interface, then its address in the IP address
                // table: loopback for 127/8, the card for everything else.
                const Adapter adapters = Adapters(*net);
                U local = 0;
                if ((destination >> 24) == 127) local = 0x7f000001;
                else if (adapters.found && !released) local = adapters.primary.address;
                else return 0x2743;   //  WSAENETUNREACH, no route
                const unsigned char address[16] = {2, 0, 0, 0, (unsigned char)(local >> 24), (unsigned char)(local >> 16),
                                                   (unsigned char)(local >> 8), (unsigned char)local};
                Store(target, address, 16);
                bytes = 0x10;
                return 0;
            }
            case 0x28000017:   // SIO_ADDRESS_LIST_CHANGE
            case 0x88000015:   // SIO_ROUTING_INTERFACE_CHANGE
                if (command == 0x88000015)
                {
                    if (source_length < 0x10 || !source) return Wsaefault;
                    if (!w.Readable(source, 2) || Read32(source) % 0x10000 != 2) return Wsaeafnosupport;
                }
                // The notification is queued with the provider and reported
                // as WSAEWOULDBLOCK; the host's addresses and routes do not
                // change under a running session, so it ends with the socket.
                return Wsaewouldblock;
            default:
                return Wsaeinval;
            }
        };
        error = provider();
        if (!error)
        {
            out(0x1c, bytes);
            if (overlapped)
            {
                // Done at once. Windows 98 queues the completion APC twice
                // here (same routine and context), unlike every other
                // overlapped path of WSOCK2.
                Write32(overlapped + 4, bytes);
                Write32(overlapped, 0);
                if (in(0x20))
                {
                    auto &t = threads[current_thread];
                    const U args[] = {in(0x24)};
                    if (t.fiber) { EnqueueApc(current_thread, in(0x20), args, 1, 0); EnqueueApc(current_thread, in(0x20), args, 1, 0); }
                }
                else if (in(0x24))
                {
                    Wsock2::SetEvent(in(0x24));
                    Wsock2::CloseEvent(in(0x24));
                }
            }
            return 0;
        }
        if (error == Wsaewouldblock && (command == 0x28000017 || command == 0x88000015))
        {
            const U mask = command == 0x28000017 ? 0x200 : 0x100;
            if (overlapped)
            {
                //  an overlapped wait of kind 30h.
                Waiter request;
                request.kind = 0x30; request.overlapped = overlapped;
                request.procedure = in(0x20); request.context = in(0x24);
                if (!w.RegisterOverlapped(slot, mask | FdClose, request)) return Wsaenobufs;
                return 0x3e5;   // ERROR_IO_PENDING
            }
            if (Get(slot, OOptions) & Blocking)
            {
                //  a blocking socket waits for the change.
                const U id = w.Register(slot, mask, in(0x20), in(0x24));
                if (!id) return Wsaenobufs;
                out(0x28, id);
                return Pending;
            }
            return error;
        }
        if (error == Wsaewouldblock) return 0x277b;   //  WSASYSCALLFAILURE
        return error;
    }
    case 0x111:   // cancel one wait, ioctl_111
    {
        if (size < 8) return Wsaefault;
        slot = SlotOf(in(0));
        for (auto &wt : waiters)
            if (wt.used && (int)wt.slot == slot && wt.id == in(4)) wt = Waiter{};
        return 0;
    }
    case 0x112:   // WsControl, ioctl_112, answered by prov_14
        return Wsock2Control(block, size);
    case 0x113:   // POSTMESSAGE32 of USER.EXE, ioctl_113
        post_routine = size >= 4 && in(0) != 0;
        return 0;
    case 0x11a:   // catalog entry of the socket, ioctl_11a
    {
        if (size < 8) return Wsaefault;
        slot = SlotOf(in(0));
        out(4, slot >= 0 ? Get(slot, OCatalog) : 0);
        return 0;
    }
    case 0x116:   // the current thread, ioctl_116
        if (size < 4) return Wsaefault;
        out(0, 0xc7000000u | (ProcessId() & 0xffff) << 8 | current_thread);
        return 0;
    case 0x117:   // an APC to that thread, ioctl_117
    {
        if (size < 0xc) return Wsaefault;
        const U thread = in(0);
        if ((thread >> 24) != 0xc7 || ((thread >> 8) & 0xffff) != (ProcessId() & 0xffff) || (thread & 0xff) >= Application::ThreadCapacity)
            return Wsaefault;
        const U args[] = {in(8)};
        return EnqueueApc(thread & 0xff, in(4), args, 1, 0) ? 0 : Wsaefault;
    }
    default:
    {
        // 114h and 115h have no caller in MSAFD.
        char message[120];
        snprintf(message, sizeof message, "WSOCK2 ioctl %03xh is not built: no caller in MSAFD.DLL was found", code);
        Fail(message);
        return Wsaenetdown;
    }
    }
}

// DIOC_CLOSEHANDLE: the sockets of the process close hard.
void Application::Wsock2CloseDevice()
{
    NetHost::Sockets *net = NetHost::Current();
    if (!net || !arena) return;
    Wsock2 w{*this, *net};
    for (U i = 0; i < Slots; ++i)
        if (natives[i].used && natives[i].owner == this)
        {
            Set(i, OLinger, 1);
            w.Destroy(i, true);
        }
}
void Application::FreeWsock2()
{
    for (auto &wt : waiters)
        if (wt.used && wt.app == this) wt = Waiter{};
    Wsock2CloseDevice();
}
// Whether a thread of this process waits for the network: a blocking call or a
// select with a wait, a connect on its way, a name question at the resolver.
bool Application::NetworkPending() const
{
    if (!arena) return false;
    for (const auto &wt : waiters)
    {
        if (!wt.used || wt.app != this) continue;
        // WININET wakes its own select through a datagram socket connected
        // to itself; a wait on that one only waits for the process itself.
        const auto &n = natives[wt.slot];
        if (n.peer && !Stream(wt.slot) && !memcmp(Bytes(wt.slot, OLocalAddress), Bytes(wt.slot, ORemoteAddress), 8))
            continue;
        return true;
    }
    for (U i = 0; i < Slots; ++i)
        if (natives[i].used && natives[i].owner == this &&
            (Get(i, OState) == Connecting || natives[i].lookup >= 0))
            return true;
    return false;
}
// The transport indications, for the sockets of this process.
void Application::TickNetwork()
{
    NetHost::Sockets *net = NetHost::Current();
    if (!net || !arena) return;
    const unsigned long long now = Now();
    if (now - network_tick < 1000) return;
    network_tick = now;
    Wsock2 w{*this, *net};
    for (U i = 0; i < Slots; ++i)
        if (natives[i].used && natives[i].owner == this) w.Poll(i);
}

// WsControl action 2, the ICMP echo of ICMP.DLL on Windows 9x: IcmpSendEcho
// ICMP.DLL calls WsControl(6, 2, request, &length, reply, &length), and VIP
// sends the echo and answers when the reply or an ICMP error arrives or the
// timeout runs out. The request is {address, timeout, data offset (word),
// data size (word), options valid, TTL, TOS, flags, options offset (word),
// options size, pad} with the options and the data behind it; the reply is
// one ICMP_ECHO_REPLY {address, status, round trip, data size (word), reply
// count (word), data, TTL, TOS, flags, options size, options} followed by the
// echoed data. The two pointers are offsets from the reply entry, which
// ICMP.DLL converts into addresses. A reply count of zero makes ICMP.DLL set
// the status as the last error. The host side is an ICMP socket that needs
// no rights (NetHost::Sockets::OpenIcmp); the calling thread sleeps like
// Sleep between looks at it.
namespace
{
enum : U
{
    IpSuccess = 0, IpBufTooSmall = 11001, IpDestNetUnreachable = 11002, IpDestHostUnreachable = 11003,
    IpDestProtUnreachable = 11004, IpDestPortUnreachable = 11005, IpPacketTooBig = 11009,
    IpReqTimedOut = 11010, IpTtlExpiredTransit = 11013, IpTtlExpiredReassem = 11014, IpParamProblem = 11015,
    IpSourceQuench = 11016, IpGeneralFailure = 11050
};
unsigned short EchoSequence = 0;
// ICMP counters for the MIBICMPINFO of entity 380h: echo requests sent,
// echo replies and errors received, by this session's echoes.
U IcmpOutMessages = 0, IcmpOutEchos = 0, IcmpInMessages = 0, IcmpInEchoReplies = 0, IcmpInErrors = 0,
  IcmpInUnreachable = 0, IcmpInTimeExceeded = 0;
unsigned short Checksum(const unsigned char *p, U n)
{
    U sum = 0;
    for (U i = 0; i + 1 < n; i += 2) sum += (U)p[i] << 8 | p[i + 1];
    if (n & 1) sum += (U)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short)~sum;
}
// Status of an ICMP error message (type, code), as VIP names them.
U ErrorStatus(unsigned char type, unsigned char code)
{
    if (type == 3)
        switch (code)
        {
        case 0: return IpDestNetUnreachable;
        case 2: return IpDestProtUnreachable;
        case 3: return IpDestPortUnreachable;
        case 4: return IpPacketTooBig;
        default: return IpDestHostUnreachable;
        }
    if (type == 11) return code ? IpTtlExpiredReassem : IpTtlExpiredTransit;
    if (type == 12) return IpParamProblem;
    if (type == 4) return IpSourceQuench;
    return 0;
}
}

// The three WSAPROTOCOL_INFOW of WSOCK2.VXD (object 0 + 452Ch, 274h bytes
// each, TCP, UDP and raw), read from the LE file of the tree.
bool Application::Wsock2ProtocolTemplate(unsigned index, unsigned char *out)
{
    char full[260] = {};
    unsigned node = 0;
    if (files.FullPath("C:\\WINDOWS\\SYSTEM\\WSOCK2.VXD", full) || files.store.Lookup(full, node)) return false;
    const auto *record = files.store.Get(node);
    const unsigned char *d = record ? files.store.Bytes(node) : nullptr;
    if (!d) return false;
    const U size = record->size;
    auto u32 = [&](U at) { U v = 0; if (at + 4 <= size) memcpy(&v, d + at, 4); return v; };
    const U h = u32(0x3c);
    if (h + 0x84 > size || d[h] != 'L' || d[h + 1] != 'E') return false;
    const U page = u32(h + 0x28), objects = h + u32(h + 0x40), pages = u32(h + 0x80);
    const U first = u32(objects + 12);   // object 0: page table index, 1-based
    if (!page || !first) return false;
    for (U i = 0; i < 0x274; ++i)
    {
        const U offset = 0x452c + index * 0x274 + i;
        const U at = pages + (first - 1 + offset / page) * page + offset % page;
        if (at >= size) return false;
        out[i] = d[at];
    }
    // "MS.w95.spi." at szProtocol (+74h), UTF-16.
    static const char tag[] = "MS.w95.spi.";
    for (U i = 0; tag[i]; ++i)
        if (out[0x74 + i * 2] != (unsigned char)tag[i] || out[0x75 + i * 2]) return false;
    return true;
}

unsigned Application::Wsock2Echo(unsigned in, unsigned in_length, unsigned out, unsigned capacity, unsigned block)
{
    NetHost::Sockets *net = NetHost::Current();
    if (in_length < 0x14 || !virtual_memory.Access(in, 0x14, false) || capacity < 0x1c ||
        !virtual_memory.Access(out, capacity, true))
        return TdiInvalidQuery;
    unsigned char head[0x14];
    if (!Read(in, head, sizeof head)) return TdiInvalidQuery;
    // The address is in network order, as inet_addr gives it.
    const U address = Net(Read32(in)), timeout = Read32(in + 4);
    const U data_offset = head[8] | head[9] << 8, data_size = head[0xa] | head[0xb] << 8;
    const bool options = head[0xc] != 0;
    const U ttl = head[0xd], tos = head[0xe], flags = head[0xf];
    static unsigned char packet[65536 + 64], answer[65536 + 128];
    if (data_size > 65500 || data_offset + data_size > in_length) return TdiInvalidQuery;
    const unsigned short sequence = ++EchoSequence, ident = 0x9800 | (current_thread & 0xff);
    packet[0] = 8;
    packet[1] = 0;
    packet[2] = packet[3] = 0;
    packet[4] = ident >> 8; packet[5] = ident & 0xff;
    packet[6] = sequence >> 8; packet[7] = sequence & 0xff;
    if (data_size && !Read(in + data_offset, packet + 8, data_size)) return TdiInvalidQuery;
    const unsigned short sum = Checksum(packet, 8 + data_size);
    packet[2] = sum >> 8; packet[3] = sum & 0xff;

    U status = IpGeneralFailure, from = 0, round_trip = 0, echoed = 0, reply_ttl = 0, reply_tos = 0;
    const unsigned char *echo_data = nullptr;
    const int id = net ? net->OpenIcmp() : -NetHost::NotSupported;
    const unsigned long long start = Now(), end = start + (unsigned long long)timeout * 1000;
    if (id >= 0)
    {
        if (options)
        {
            net->SetOption(id, NetHost::Ttl, ttl ? (int)ttl : 128);
            net->SetOption(id, NetHost::TypeOfService, (int)tos);
            net->SetOption(id, NetHost::DontFragment, (flags & 2) ? 1 : 0);
        }
        const int sent = net->SendTo(id, packet, 8 + data_size, 0, address, 0);
        if (sent >= 0) { ++IcmpOutMessages; ++IcmpOutEchos; }
        status = sent < 0 ? (sent == -NetHost::NetworkUnreachable ? IpDestNetUnreachable
                             : sent == -NetHost::HostUnreachable ? IpDestHostUnreachable
                             : sent == -NetHost::MessageSize ? IpPacketTooBig : IpGeneralFailure)
                          : IpReqTimedOut;
        while (sent >= 0)
        {
            U sender = 0, port = 0;
            const int got = net->Receive(id, answer, sizeof answer, 0, &sender, &port);
            if (got > 0)
            {
                // The macOS socket hands the IP header over; a host without it
                // starts with the ICMP header.
                U header = 0, hops = 0, service = 0;
                if (got >= 20 && (answer[0] >> 4) == 4) { header = (answer[0] & 15) * 4; hops = answer[8]; service = answer[1]; }
                const unsigned char *icmp = answer + header;
                const U length = got > (int)header ? (U)got - header : 0;
                if (length >= 8 && icmp[0] == 0 && (icmp[6] << 8 | icmp[7]) == sequence && sender == address)
                {
                    status = IpSuccess;
                    echo_data = icmp + 8;
                    echoed = length - 8;
                }
                else if (length >= 8 + 20 + 8 && ErrorStatus(icmp[0], icmp[1]))
                {
                    // The error quotes our IP header and the first eight bytes
                    // of our echo request.
                    const unsigned char *quoted = icmp + 8;
                    const U inner = (quoted[0] & 15) * 4;
                    const unsigned char *request = quoted + inner;
                    if (8 + inner + 8 <= length && request[0] == 8 && (request[6] << 8 | request[7]) == sequence)
                        status = ErrorStatus(icmp[0], icmp[1]);
                }
                if (status != IpReqTimedOut)
                {
                    ++IcmpInMessages;
                    if (status == IpSuccess) ++IcmpInEchoReplies;
                    else if (status == IpTtlExpiredTransit || status == IpTtlExpiredReassem) ++IcmpInTimeExceeded;
                    else ++IcmpInUnreachable;
                    from = sender;
                    round_trip = (U)((Now() - start) / 1000);
                    reply_ttl = hops;
                    reply_tos = service;
                    break;
                }
                continue;
            }
            if (got < 0 && got != -NetHost::WouldBlock) { status = IpGeneralFailure; break; }
            const unsigned long long now = Now();
            if (now >= end) break;
            auto &t = threads[current_thread];
            t.block = 3;
            t.deadline = now + 1000 < end ? now + 1000 : end;
            Suspend(Idle);
            if (state == Failed || state == Exited) break;
        }
        net->Close(id);
    }
    else
        status = IpGeneralFailure;

    U data_bytes = echoed;
    if (0x1c + data_bytes > capacity) { status = status == IpSuccess ? IpBufTooSmall : status; data_bytes = 0; }
    unsigned char reply[0x1c] = {};
    auto put32 = [&](U at, U v) { memcpy(reply + at, &v, 4); };
    put32(0, status == IpReqTimedOut || status == IpGeneralFailure ? 0 : Net(from));
    put32(4, status);
    put32(8, round_trip);
    reply[0xc] = data_bytes & 0xff; reply[0xd] = data_bytes >> 8;
    const U count = status == IpReqTimedOut || status == IpGeneralFailure ? 0 : 1;
    reply[0xe] = (unsigned char)count;
    put32(0x10, 0x1c);   // the data follow the entry
    reply[0x14] = (unsigned char)reply_ttl;
    reply[0x15] = (unsigned char)reply_tos;
    Store(out, reply, sizeof reply);
    if (data_bytes && echo_data) Store(out + 0x1c, echo_data, data_bytes);
    Write32(block + 0xc, 0x1c + data_bytes);
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "WSOCK2 echo to %08x status=%u from=%08x ms=%u bytes=%u ttl=%u", address, status,
                 from, round_trip, data_bytes, reply_ttl);
        log(text);
    }
    return 0;
}

// WsControl, ioctl_112 : WSOCK2 hands it to prov_14 of AFVXD, which
// calls TdiQueryInformationEx of MSTCP (VTCP, VIP behind it). The
// block is {in, out, *inLen, *outLen, protocol, action}. The answers are the
// TDI objects the callers read: the entity list, the entity types, the IP
// statistics, the address and route tables and the interface entries.
unsigned Application::Wsock2Control(unsigned block, unsigned size)
{
    NetHost::Sockets *net = NetHost::Current();
    if (size < 0x18 || !net) return Wsaefault;
    const U in = Read32(block), out = Read32(block + 4), in_length = Read32(block + 8);
    const U capacity = Read32(block + 0xc), action = Read32(block + 0x14);
    if (action == 2) return Wsock2Echo(in, in_length, out, capacity, block);
    if (action != 0) return TdiInvalidRequest;   // SET: the host's tables are not the guest's to change
    if (in_length < 0x14 || !virtual_memory.Access(in, 0x14, false)) return TdiInvalidQuery;
    const U entity = Read32(in), instance = Read32(in + 4), klass = Read32(in + 8), type = Read32(in + 0xc),
            id = Read32(in + 0x10);
    const Adapter adapters = Adapters(*net);
    const bool up = adapters.found && !released;
    unsigned char answer[0x400];
    U bytes = 0;
    memset(answer, 0, sizeof answer);
    auto put = [&](U offset, U value) { memcpy(answer + offset, &value, 4); if (offset + 4 > bytes) bytes = offset + 4; };
    if (type != 0x100) return TdiInvalidQuery;
    const U interfaces = adapters.found ? 2 : 1;
    if (entity == 0 && klass == 0x100 && id == 0)
    {
        // The entity list: VTCP puts in its two, VIP appends its own.
        const U list[][2] = {{0x400, 0}, {0x401, 0}, {0x301, 0}, {0x200, 0}, {0x200, 1}};
        for (U i = 0; i < 3 + interfaces; ++i) { put(i * 8, list[i][0]); put(i * 8 + 4, list[i][1]); }
    }
    else if (klass == 0x100 && id == 1)
    {
        // ENTITY_TYPE_ID
        if ((entity == 0x200 && instance < interfaces) || ((entity == 0x301 || entity == 0x400 || entity == 0x401) && !instance))
            put(0, entity == 0x200 ? 0x202 : entity == 0x301 ? 0x303 : entity == 0x400 ? 0x404 : 0x403);
        else return TdiInvalidRequest;
    }
    else if (entity == 0x301 && !instance && klass == 0x200 && id == 1)
    {
        // IPSNMPInfo, 5Ch bytes: not forwarding, TTL 32, the three counts.
        put(0, 2); put(4, 32);
        put(0x50, interfaces);
        put(0x54, up ? 2 : 1);
        put(0x58, up ? 3 : 1);
    }
    else if (entity == 0x301 && !instance && klass == 0x200 && id == 0x102)
    {
        // IPAddrEntry: address, interface index, mask, broadcast bit, reassembly size.
        put(0, Net(0x7f000001)); put(4, 1); put(8, Net(0xff000000)); put(0xc, 1); put(0x10, 0xffff);
        if (up)
        {
            put(0x18, Net(adapters.primary.address)); put(0x1c, 2); put(0x20, Net(adapters.primary.mask));
            put(0x24, 1); put(0x28, 0xffff);
        }
        bytes = (up ? 2 : 1) * 0x18;
    }
    else if (entity == 0x301 && !instance && klass == 0x200 && id == 0x103)
    {
        // VIP : IP_INTFC_INFO_ID looks up the active
        // address from the query context, then copies IPInterfaceInfo and
        // its hardware address. No partial output on a small buffer.
        Write32(block + 0xc, 0);
        if (in_length < 0x18 || !virtual_memory.Access(in + 0x14, 4, false)) return TdiInvalidQuery;
        const U address = Net(Read32(in + 0x14));
        const bool loop = address == 0x7f000001;
        if (!loop && (!up || address != adapters.primary.address)) return 0x1a;
        if (capacity < 16) return 0x21;
        const U hardware_bytes = loop ? 0 : sizeof adapters.primary.hardware;
        if (capacity < 16 + hardware_bytes) { Write32(block + 0xc, 16); return 0x21; }
        put(0, 0); // Neither the loopback nor our Ethernet adapter is point-to-point.
        put(4, loop ? 256 : adapters.primary.mtu);
        put(8, 10000000);
        put(12, hardware_bytes);
        if (hardware_bytes) memcpy(answer + 16, adapters.primary.hardware, hardware_bytes);
        bytes = 16 + hardware_bytes;
    }
    else if (entity == 0x301 && !instance && klass == 0x200 && id == 0x101)
    {
        // IPRouteEntry, 30h bytes, one per entry of IpRoutes.
        IpRoute routes[3];
        const U count = IpRoutes(adapters, up, routes);
        for (U i = 0; i < count; ++i)
        {
            const U at = i * 0x30;
            put(at, Net(routes[i].destination)); put(at + 4, routes[i].index);
            put(at + 8, 1); put(at + 0xc, ~0u); put(at + 0x10, ~0u); put(at + 0x14, ~0u);
            put(at + 0x18, Net(routes[i].next)); put(at + 0x1c, routes[i].kind); put(at + 0x20, 2);
            put(at + 0x24, 0); put(at + 0x28, Net(routes[i].mask)); put(at + 0x2c, ~0u);
        }
    }
    else if (entity == 0x200 && instance < interfaces && klass == 0x200 && id == 1)
    {
        // IFEntry: index, type, MTU, speed, hardware address, status, description.
        const bool loop = instance == 0;
        const auto &i = loop ? adapters.loopback : adapters.primary;
        put(0, instance + 1);
        put(4, loop ? 24 : 6);
        put(8, loop ? 1500 : i.mtu);
        put(0xc, loop ? 10000000 : 10000000);
        put(0x10, loop ? 0 : 6);
        if (!loop) memcpy(answer + 0x14, i.hardware, 6);
        put(0x1c, 1);
        put(0x20, loop || !released ? 1 : 2);
        if (!loop)
        {
            // The card's counters are the host interface's (VIP keeps the
            // ones NDIS reports for the card). SENS calls the network alive
            // only while unicast packets go in or out. A pulled cable keeps them where they were.
            static NetHost::Interface held;
            if (net->Online()) held = i;
            put(0x28, held.in_octets); put(0x2c, held.in_unicast); put(0x30, held.in_other);
            put(0x34, held.in_discards); put(0x38, held.in_errors); put(0x3c, held.in_unknown);
            put(0x40, held.out_octets); put(0x44, held.out_unicast); put(0x48, held.out_other);
            put(0x4c, held.out_discards); put(0x50, held.out_errors);
        }
        const char *text = loop ? "MS TCP Loopback interface" : AdapterDescription;
        const U length = (U)strlen(text) + 1;
        put(0x58, length);
        memcpy(answer + 0x5c, text, length);
        bytes = 0x5c + length;
    }
    else if (entity == 0x380 && !instance && klass == 0x200 && id == 1)
    {
        // MIBICMPINFO of ER_ENTITY: icmpInStats then icmpOutStats, 13 dwords
        // each {msgs, errors, dest unreachs, time excds, parm probs, src
        // quenchs, redirects, echos, echo reps, timestamps, timestamp reps,
        // addr masks, addr mask reps} (NETSTAT -s).
        put(0, IcmpInMessages); put(4, IcmpInErrors); put(8, IcmpInUnreachable); put(0xc, IcmpInTimeExceeded);
        put(0x20, IcmpInEchoReplies);
        put(0x34, IcmpOutMessages); put(0x50, IcmpOutEchos);
        bytes = 0x68;
    }
    else if (entity == 0x400 && !instance && klass == 0x200 && id == 1) bytes = 0x3c;   // TCPStats, all zero
    else if (entity == 0x401 && !instance && klass == 0x200 && id == 1) bytes = 0x14;   // UDPStats
    else if ((entity == 0x400 || entity == 0x401) && instance) return TdiInvalidRequest;
    else
    {
        char message[160];
        snprintf(message, sizeof message, "WSOCK2: WsControl query entity %x instance %u class %x id %x is not built",
                 entity, instance, klass, id);
        Fail(message);
        return Wsaenetdown;
    }
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "WSOCK2 WsControl entity=%x.%u class=%x id=%x bytes=%u capacity=%u adapter=%08x",
                 entity, instance, klass, id, bytes, capacity, up ? adapters.primary.address : 0);
        log(text);
    }
    if (bytes > capacity) { Write32(block + 0xc, bytes); return TdiBufferOverflow; }
    if (bytes && (!virtual_memory.Access(out, bytes, true) || !Store(out, answer, bytes))) return TdiInvalidQuery;
    Write32(block + 0xc, bytes);
    return TdiSuccess;
}

// VDHCP code 4, the DHCPCSVC requests. The buffer is {capacity,
// length, records}, both dwords in network order; a record is
// {type byte, length in network order, bytes}. The first record
// whose type is below 9 is the command; the others carry its
// parameters: 9 the class ID, 0Ah the options to send, 0Bh the options to
// request (37h, then the option numbers), 0Ch to 0Eh the change notification.
// The answer goes back over the same buffer: {status, length} in network
// order and the reply behind them.
// 0 renew, 1 release the lease of the adapter the record names;
// 2, 3 ERROR_CALL_NOT_IMPLEMENTED;
// 5, 6 the options of the lease that were asked for:
// per option a record {0Bh, length in network order, option number,
// value}, the length of all of them in the answer's length field;
// 7, 8 register and unregister a change notification: the handle record 0Eh is required, a record twice is 87.
// The lease of this port never changes, so a registration never fires.
unsigned Application::VdhcpRequest(unsigned buffer, unsigned size, bool found, unsigned address,
                                   unsigned mask, unsigned gateway)
{
    auto swap = [](U v) { return v >> 24 | (v >> 8 & 0xff00) | (v << 8 & 0xff0000) | v << 24; };
    if (!buffer || size < 8) return 0x6f8;
    const U capacity = swap(Read32(buffer)), length = swap(Read32(buffer + 4));
    auto answer = [&](U status, U bytes) {
        Write32(buffer, swap(status));
        Write32(buffer + 4, swap(bytes));
        return 0u;
    };
    if (length > size - 8 || capacity > size - 8) return answer(0xd, 0);   // ERROR_INVALID_DATA
    struct Record { unsigned char type; U length, data; };
    Record records[32];
    U count = 0;
    for (U at = 0; at < length;)
    {
        if (at + 5 > length) return answer(0x57, 0);
        unsigned char type = 0;
        Read(buffer + 8 + at, &type, 1);
        const U bytes = swap(Read32(buffer + 9 + at));
        if (at + 5 + bytes > length) return answer(0x57, 0);
        if (count < 32) records[count++] = {type, bytes, buffer + 13 + at};
        at += 5 + bytes;
    }
    const Record *command = nullptr;
    for (U i = 0; i < count && !command; ++i)
        if (records[i].type < 9) command = &records[i];
    if (!command) return answer(0x57, 0);
    const bool dhcp = found;
    switch (command->type)
    {
    case 0: case 1:
        // The adapter name is Unicode; an odd length is 87.
        if ((command->length & 1) || command->length < 2) return answer(0x57, 0);
        if (!dhcp) return answer(2, 0);
        if (command->type == 1) released = true;
        else released = false;
        return answer(0, 0);
    case 2: case 3:
        return answer(0x78, 0);
    case 5: case 6:
    {
        if (!dhcp || released) return answer(2, 0);
        // The lease's options, values in network order.
        const U net_mask = Net(mask), net_address = Net(address);
        const U router = Net(gateway ? gateway : address);
        const U dns = Net(DnsServer), forever = 0xffffffffu;
        struct Option { unsigned char id; U value; } lease[] = {{1, net_mask}, {3, router}, {6, dns}, {51, forever},
                                                              {54, dns}, {50, net_address}};
        unsigned char reply[512];
        U used = 0;
        for (U i = 0; i < count; ++i)
        {
            if (records[i].type != 0xb || !records[i].length) continue;
            unsigned char list[256];
            const U n = records[i].length < sizeof list ? records[i].length : sizeof list;
            Read(records[i].data, list, n);
            for (U k = list[0] == 0x37 ? 1 : 0; k < n; ++k)
                for (const auto &option : lease)
                    if (option.id == list[k] && used + 10 <= sizeof reply)
                    {
                        reply[used] = 0xb;
                        const U record_length = swap(5);
                        memcpy(reply + used + 1, &record_length, 4);
                        reply[used + 5] = option.id;
                        memcpy(reply + used + 6, &option.value, 4);
                        used += 10;
                    }
        }
        // The reply grows the length field of the answer itself; the
        // records follow it directly.
        if (used > size - 8) return answer(0xea, used);
        Store(buffer + 8, reply, used);
        return answer(0, used);
    }
    case 7: case 8:
    {
        bool handle = false, seen[3] = {};
        for (U i = 0; i < count; ++i)
            if (records[i].type >= 0xc && records[i].type <= 0xe)
            {
                if (seen[records[i].type - 0xc]) return answer(0x57, 0);
                seen[records[i].type - 0xc] = true;
                if (records[i].type == 0xe) handle = true;
            }
        return answer(handle ? 0 : 0x57, 0);
    }
    default:
        return answer(0x57, 0);
    }
}

// \\.\VDHCP: the lease VDHCP.386 holds for the adapter. The network the provider stands
// for gives the host's address and names its own DNS server (option 6).
bool Application::VdhcpAvailable() const { return NetHost::Current() != nullptr; }
unsigned Application::VdhcpVxdControl(const unsigned *a)
{
    NetHost::Sockets *net = NetHost::Current();
    const U code = a[1], buffer = a[2], size = a[3];
    if (!net) return 1;
    if (code == 0 || code == ~0u) return 0;
    if (code < 1 || code > 4) return 1;
    if (buffer && !virtual_memory.Access(buffer, size, true)) return 0x6f8;
    const Adapter adapters = Adapters(*net);
    if (code == 4)
        return VdhcpRequest(buffer, size, adapters.found, adapters.primary.address, adapters.primary.mask,
                            adapters.primary.gateway);
    if (code == 2 || code == 3)
    {
        // {hwOffset, hwLen, hw}: the adapter by its hardware address.
        if (!buffer || size < 8 || !adapters.found) return 3;
        const U offset = Read32(buffer), length = Read32(buffer + 4);
        unsigned char hardware[16] = {};
        if (length != 6 || offset + length > size || !Read(buffer + offset, hardware, 6) ||
            memcmp(hardware, adapters.primary.hardware, 6))
            return 3;
        released = code == 3;
        return 0;
    }
    // Code 1: count, one 2Ch record per adapter, then the data.
    const U records = adapters.found ? 1 : 0;
    const U need = 4 + records * (0x2c + 6 + 4);
    if (!buffer) return 0x6f;
    if (size < need)
    {
        if (size >= 4) Write32(buffer, need);
        return 0x6f;
    }
    unsigned char blob[4 + 0x2c + 6 + 4] = {};
    auto put = [&](U offset, U value) { memcpy(blob + offset, &value, 4); };
    put(0, records);
    if (records)
    {
        const U data = 4 + 0x2c;
        // Seconds since 1 January 1980, local time, as the VDHCP clock counts.
        const unsigned long long local = CurrentFileTime() - (unsigned long long)(int)EffectiveTimeBias() * 600000000ull;
        const U obtained = (U)((local - 119600064000000000ull) / 10000000ull);
        put(4, data); put(8, 6);
        put(0xc, released ? 0 : Net(adapters.primary.address));
        put(0x10, 0xffffffffu);            // an infinite lease
        put(0x14, obtained);
        put(0x18, 0x7fffffffu);
        put(0x1c, Net(DnsServer));         // the DHCP server is the host's service address
        put(0x20, 4); put(0x24, data + 6); // option 6: one DNS server
        put(0x28, 0); put(0x2c, 0);        // option 15: no domain
        memcpy(blob + data, adapters.primary.hardware, 6);
        put(data + 6, Net(DnsServer));
    }
    if (!Store(buffer, blob, need)) return 0x6f8;
    return 0;
}

// \\.\VIP: the W32_DEVICEIOCONTROL interface of VIP.386. IPHLPAPI opens it in
// its DllMain (device 489h); without it every IP Helper call
// answers ERROR_NOT_SUPPORTED in GetIfTable, and the SENS of
// WEBCHECK in Explorer reports no network to WININET.
bool Application::VipAvailable() const { return NetHost::Current() != nullptr; }
unsigned Application::VipVxdControl(const unsigned *a)
{
    NetHost::Sockets *net = NetHost::Current();
    const U code = a[1], in = a[2], in_size = a[3], out = a[4], out_size = a[5], returned = a[6];
    if (!net) return 0x32;
    if (code == 0 || code == ~0u) return 0;   // DIOC_OPEN and DIOC_CLOSEHANDLE
    // The buffers and the count are locked first.
    if ((in && !virtual_memory.Access(in, in_size, false)) || (out && !virtual_memory.Access(out, out_size, true)) ||
        (returned && !virtual_memory.Access(returned, 4, true)))
        return 0x6f8;
    auto count = [&](U bytes) { if (returned) Write32(returned, bytes); };
    const Adapter adapters = Adapters(*net);
    const bool up = adapters.found && !released;
    // The interfaces of VIP, the loopback  included.
    const U interfaces = up ? 2 : 1;
    switch (code & 0xffff)
    {
    case 0x67:   // SendARP: not built in VIP
        return in && out ? 0x32 : 0x57;
    case 0x68:   // GetInterfaceInfo
    {
        if (!out) return 0x57;
        // The count leaves out the loopback, the size check and the count of
        // bytes do not.
        if (out_size >= 4) Write32(out, interfaces - 1);
        U result = 0x6f;
        if ((interfaces - 1) * 0x104 + 4 <= out_size)
        {
            // IP_ADAPTER_INDEX_MAP: the index, then the name VIP took over
            // from the binding. ARP's bind handler hands the
            // adapter name NDIS gave it to IPAddInterface, which
            // keeps a copy of the text at interface +0A8h and its length at
            // +0A4h. NDIS names the card after its
            // software key, Class\Net\0001: "0001" (src/neo98_hardware.h).
            // The text is ANSI; VIP copies its bytes as they are into the
            // WCHAR field and ends them with a zero word at the length
            // rounded down to even, and IPHLPAPI passes
            // the buffer on unchanged.
            const char *name = strrchr(Neo98Hardware::NetDriver, '\\') + 1;
            const U length = U(strlen(name));
            for (U i = 1; i < interfaces; ++i)
            {
                const U at = out + 4 + (i - 1) * 0x104;
                const unsigned short end = 0;
                Write32(at, i + 1);
                Store(at + 4, name, length);
                Store(at + 4 + (length & ~1u), &end, 2);
            }
            result = 0;
        }
        count(interfaces * 0x104 + 4);
        return result;
    }
    case 0x69:   // GetBestInterface: the interface of the route
    {
        if (!in || !out || in_size != 4 || out_size != 4) return 0x57;
        const U destination = Net(Read32(in));
        IpRoute routes[3];
        const U n = IpRoutes(adapters, up, routes);
        // The most specific route that matches.
        int best = -1;
        for (U i = 0; i < n; ++i)
            if ((destination & routes[i].mask) == routes[i].destination &&
                (best < 0 || routes[i].mask > routes[best].mask))
                best = int(i);
        if (best < 0) return 0x4cf;   // ERROR_NETWORK_UNREACHABLE
        Write32(out, routes[best].index);
        count(4);
        return 0;
    }
    case 0x6a:   // GetUniDirectionalAdapterInfo: no interface has flag 4
        if (out_size < 4) { count(4); return 0x6f; }
        if (!out) return 0x57;
        Write32(out, 0);
        count(4);
        return 0;
    default:
        return 0x32;
    }
}
} // namespace X86
