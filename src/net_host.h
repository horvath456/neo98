// SPDX-License-Identifier: GPL-2.0-or-later
// The host side of the network: what the native WSOCK2.VXD (src/x86_wsock2.cpp)
// asks of the machine it runs on.
// Under Windows 98 the socket layer of WSOCK2.VXD hands every socket to an
// address-family provider. For TCP/IP that is AFVXD.VXD with its helper
// WSHTCP.VXD, and below them VTDI.386 and the MSTCP stack (VTCP.386, VIP.386)
// speak to the network card through NDIS. This port keeps WSOCK2 and replaces
// that whole provider column by the sockets of the host: the host already has
// a TCP/IP stack, so none is rebuilt here. The implementation below shows where the
// edge lies and why.
// src/ knows nothing about the host's socket calls. A host installs an object
// of this interface (tools/host_net.h has the POSIX one); without it the
// device \\.\WSOCK2 does not exist, exactly like a Windows 98 installation
// without networking.
// IPv4 addresses and ports use host byte order.
#pragma once
namespace NetHost
{
typedef unsigned U;
// Portable error classes. The native WSOCK2 maps them onto the WSAE* codes of
// Windows 98; a host maps its errno values onto them.
enum Error : int
{
    Ok = 0,
    WouldBlock = 1,
    InProgress = 2,
    Already = 3,
    NotConnected = 4,
    IsConnected = 5,
    ConnectionRefused = 6,
    ConnectionReset = 7,
    ConnectionAborted = 8,
    TimedOut = 9,
    NetworkUnreachable = 10,
    HostUnreachable = 11,
    AddressInUse = 12,
    AddressNotAvailable = 13,
    Access = 14,
    Invalid = 15,
    MessageSize = 16,
    NoBuffers = 17,
    Shutdown = 18,
    NotSupported = 19,
    Other = 20,
};
// Readiness bits of Poll.
enum : U
{
    Readable = 1,     // data, a pending connection on a listener, or end of stream
    Writable = 2,     // room to send; a connect in progress has finished
    Exception = 4,    // out-of-band data waits
    Failed = 8,       // a pending error (SO_ERROR) is set
    HungUp = 16,      // the peer closed its side
};
// Socket options the provider passes through. The values are the host's
// business; the guest side maps SOL_SOCKET / IPPROTO_TCP names onto these.
enum Option : int
{
    ReuseAddress = 1,
    KeepAlive = 2,
    Broadcast = 3,
    NoDelay = 4,
    ReceiveBuffer = 5,
    SendBuffer = 6,
    OutOfBandInline = 7,
    Linger = 8,          // value: seconds, or -1 for off
    DontRoute = 9,
    PendingError = 10,   // read only: SO_ERROR, as an Error class
    MulticastTtl = 11,
    Ttl = 12,
    TypeOfService = 13,
    DontFragment = 14,
    MulticastLoop = 15,
    SharePort = 16,     // internal host support for Win98's one-sided SO_REUSEADDR
    MulticastInterface = 17,   // IP_MULTICAST_IF; value: the IPv4 address in host order, 0 for the default
};
enum SendFlags : int { SendOutOfBand = 1, SendDontRoute = 4 };
enum ReceiveFlags : int { ReceiveOutOfBand = 1, ReceivePeek = 2 };
struct Interface
{
    U address = 0, mask = 0, broadcast = 0, mtu = 1500;
    U gateway = 0;   // the host's default route through this interface, 0 when none
    unsigned char hardware[6] = {};
    bool loopback = false;
    char name[32] = {};
    // The card's counters since the host started it (the host's link
    // statistics), as MIB-II counts them: unicast and other packets, octets,
    // errors, drops and frames of an unknown protocol.
    U in_octets = 0, in_unicast = 0, in_other = 0, in_discards = 0, in_errors = 0, in_unknown = 0;
    U out_octets = 0, out_unicast = 0, out_other = 0, out_discards = 0, out_errors = 0;
};
class Sockets
{
  public:
    virtual ~Sockets() {}
    // A new non-blocking socket, stream (TCP) or datagram (UDP). Returns an
    // identifier of zero or more, or the negative Error.
    virtual int Open(bool stream) = 0;
    // A non-blocking ICMP socket for echo requests, the kind a host lets any
    // user open (macOS: SOCK_DGRAM with IPPROTO_ICMP). SendTo takes the ICMP
    // message, Receive returns the whole IP datagram of an answer: an echo
    // reply or an error that quotes the request.
    virtual int OpenIcmp() { return -NotSupported; }
    virtual void Close(int id) = 0;
    // Every call below returns zero or a byte count, or the negative Error.
    virtual int Bind(int id, U address, U port) = 0;
    virtual int Listen(int id, int backlog) = 0;
    virtual int Accept(int id, U &address, U &port) = 0;   // the new identifier
    virtual int Connect(int id, U address, U port) = 0;   // InProgress while it runs
    virtual int Send(int id, const void *data, U bytes, int flags) = 0;
    virtual int SendTo(int id, const void *data, U bytes, int flags, U address, U port) = 0;
    // `address` and `port` receive the sender when both are non-null.
    virtual int Receive(int id, void *data, U bytes, int flags, U *address, U *port) = 0;
    virtual int Shutdown(int id, int how) = 0;   // 0 receive, 1 send, 2 both
    virtual int Name(int id, bool peer, U &address, U &port) = 0;
    virtual int SetOption(int id, Option option, int value) = 0;
    // IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP: `group` and `local` in host
    // order, `local` 0 for the interface of the host's route to the group.
    virtual int Membership(int id, bool join, U group, U local)
    {
        (void)id; (void)join; (void)group; (void)local;
        return -NotSupported;
    }
    virtual int GetOption(int id, Option option, int &value) = 0;
    virtual int Pending(int id) = 0;       // FIONREAD
    virtual int AtMark(int id) = 0;        // SIOCATMARK: 1 at the mark, 0 before it
    virtual U Poll(int id) = 0;            // the readiness bits above, now
    // Blocks the calling host thread until one of the open sockets changes or
    // `milliseconds` pass. A runner that has nothing to run calls it instead
    // of sleeping.
    virtual void Wait(U milliseconds) = 0;
    // The resolver of the host, asynchronous: a lookup can take seconds and
    // the guest keeps running meanwhile. Query starts a forward lookup of
    // `name`, or a reverse lookup of `address` when `name` is null, and
    // returns a ticket of zero or more (negative: no room). Answer returns
    // 0 while the lookup runs, 1 with the result (addresses in `out` for a
    // forward lookup, the name in `text` for both) and -1 when the host knows
    // no answer; after 1 or -1 the ticket is gone.
    virtual int Query(const char *name, U address) = 0;
    virtual int Answer(int ticket, U *out, U capacity, U &count, char *text, U size) = 0;
    virtual U Interfaces(Interface *out, U capacity) = 0;
    virtual bool HostName(char *name, U size) = 0;
    // The listening policy: a guest socket that binds to `address` for
    // listening gets the address this returns. The default answer turns the
    // wildcard into 127.0.0.1; a user who wants the guest reachable from the
    // network sets that on the host.
    virtual U ListenAddress(U address) = 0;
    // The network cable (the host's menu Network). While
    // it is out, the adapter keeps its address and the loopback works, but
    // nothing reaches the network: the provider drops what goes out and
    // what comes in, and the DNS server of the lease answers nothing.
    virtual bool Online() { return true; }
};
// The installed host, or null. The runner installs one before the first
// guest starts; the pointer stays valid for the life of the process.
Sockets *Current();
void Install(Sockets *);
}
