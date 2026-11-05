// SPDX-License-Identifier: GPL-2.0-or-later
// POSIX implementation of the network host of src/net_host.h.
// Every guest socket is one non-blocking BSD socket of the host. Names go
// through getaddrinfo and getnameinfo, so the guest sees what the Mac's own
// resolver answers, /etc/hosts and the VPN's split DNS included. Nothing here
// is macOS-specific: a Linux host includes the same header.
// Wildcard guest listeners use 127.0.0.1 by default. The host menu,
// --net-listen and NEO98_NET_LISTEN select another address ("any" for all
// interfaces). Explicit guest bind addresses stay unchanged.
// SetOnline(false) pulls the network cable of the guest's card (the host's
// menu Network). The card keeps its address, the loopback and the host's own
// addresses stay reachable, and everything else behaves like a wire that
// leads nowhere: a TCP connection attempt gets no answer and ends with
// TimedOut after connect_timeout, a connection that stands sees nothing come
// and can send nothing until the cable is back (its host socket stays, so it
// goes on afterwards as TCP does after a short break), datagrams and echo
// requests to the network are lost, and whatever the network sends is
// dropped. Win98 has no media sense on this card; nothing tells the guest.
#pragma once
#include "../src/net_host.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#if defined(__APPLE__)
#include <net/route.h>
#include <sys/sysctl.h>
#endif
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <linux/if_link.h>
#endif
#if defined(__APPLE__)
#include <net/if_dl.h>
#endif

namespace HostNet
{
using NetHost::U;
class PosixSockets : public NetHost::Sockets
{
  public:
    PosixSockets()
    {
        ConfigureListen("127.0.0.1");
        // Runners without a menu (the probe of tools/test_network_cable_guest.py)
        // pull the cable with NEO98_NET_OFFLINE=1; NEO98_NET_CONNECT_TIMEOUT
        // shortens the wait of a connection attempt for such a test.
        if (const char *timeout = getenv("NEO98_NET_CONNECT_TIMEOUT"); timeout && atoi(timeout) > 0)
            connect_timeout = (unsigned)atoi(timeout);
        if (const char *off = getenv("NEO98_NET_OFFLINE"); off && !strcmp(off, "1")) SetOnline(false);
    }
    static bool ParseListen(const char *policy, U &address)
    {
        if (!strcmp(policy, "any")) { address = 0; return true; }
        in_addr parsed{};
        if (inet_pton(AF_INET, policy, &parsed) != 1) return false;
        address = ntohl(parsed.s_addr);
        return true;
    }
    // Startup: the existing environment override wins over machine/CLI settings.
    bool ConfigureListen(const std::string &policy)
    {
        const char *override = getenv("NEO98_NET_LISTEN");
        return SetListen(override && *override ? override : policy.c_str());
    }
    // Changes affect future binds/listeners. Existing sockets keep their address.
    bool SetListen(const char *policy)
    {
        U address;
        if (!ParseListen(policy, address)) return false;
        listen_address = address;
        return true;
    }
    std::string ListenPolicy() const
    {
        if (!listen_address) return "any";
        char text[INET_ADDRSTRLEN];
        in_addr address{htonl(listen_address)};
        inet_ntop(AF_INET, &address, text, sizeof text);
        return text;
    }
    ~PosixSockets() override
    {
        for (int fd : fds)
            if (fd >= 0) ::close(fd);
    }
    int Open(bool stream) override
    {
        const int fd = ::socket(AF_INET, stream ? SOCK_STREAM : SOCK_DGRAM, 0);
        if (fd < 0) return -Map(errno);
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        const int id = Keep(fd);
        held[id].stream = stream;
        return id;
    }
    // The cable. See the head of this file.
    // Seconds until a connection attempt without the cable gives up: the
    // SYN is sent again after 3, 6 and 12 seconds, as MSTCP's defaults do
    // (not measured on VTCP.386).
    unsigned connect_timeout = 21;
    void SetOnline(bool on)
    {
        if (!on)
        {
            // The host's own addresses stay local while the cable is out.
            std::lock_guard<std::mutex> hold(lock);
            own.clear();
            ifaddrs *list = nullptr;
            if (!getifaddrs(&list))
            {
                for (ifaddrs *p = list; p; p = p->ifa_next)
                    if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET)
                        own.push_back(ntohl(((const sockaddr_in *)p->ifa_addr)->sin_addr.s_addr));
                freeifaddrs(list);
            }
        }
        online = on;
    }
    bool Online() override { return online; }
    int OpenIcmp() override
    {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (getenv("NEO98_TRACE_NET")) fprintf(stderr, "NET ICMP fd=%d errno=%d\n", fd, fd < 0 ? errno : 0);
        if (fd < 0) return -Map(errno);
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        return Keep(fd);
    }
    void Close(int id) override
    {
        const int fd = Fd(id);
        if (fd < 0) return;
        ::close(fd);
        fds[id] = -1;
        held[id] = Held{};
    }
    int Bind(int id, U address, U port) override
    {
        const sockaddr_in in = Address(address, port);
        return Result(::bind(Fd(id), (const sockaddr *)&in, sizeof in));
    }
    int Listen(int id, int backlog) override { return Result(::listen(Fd(id), backlog > 0 ? backlog : 5)); }
    int Accept(int id, U &address, U &port) override
    {
        sockaddr_in in{};
        socklen_t length = sizeof in;
        const int fd = ::accept(Fd(id), (sockaddr *)&in, &length);
        if (fd < 0) return -Map(errno);
        // Without the cable no connection comes in from the network.
        if (!online && !Local(ntohl(in.sin_addr.s_addr)))
        {
            ::close(fd);
            return -NetHost::WouldBlock;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
        const int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        address = ntohl(in.sin_addr.s_addr);
        port = ntohs(in.sin_port);
        const int accepted = Keep(fd);
        held[accepted].stream = true;
        held[accepted].peer = address;
        return accepted;
    }
    int Connect(int id, U address, U port) override
    {
        if (id < 0 || (size_t)id >= held.size()) return -NetHost::Invalid;
        auto &h = held[id];
        h.peer = address;
        h.peer_port = port;
        // A stream to the network without the cable: the SYN goes nowhere.
        if (!online && h.stream && !Local(address))
        {
            h.waiting = true;
            h.timed_out = false;
            h.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(connect_timeout);
            return -NetHost::InProgress;
        }
        const sockaddr_in in = Address(address, port);
        return Result(::connect(Fd(id), (const sockaddr *)&in, sizeof in));
    }
    int Send(int id, const void *data, U bytes, int flags) override
    {
        if (Cut(id))
        {
            if (Held_(id).waiting || Held_(id).timed_out) return -NetHost::NotConnected;
            return -NetHost::WouldBlock;   // a stream waits for the cable
        }
        if (!online && id >= 0 && (size_t)id < held.size() && !held[id].stream && held[id].peer &&
            !Local(held[id].peer))
            return (int)bytes;   // a datagram is lost on the wire
        const ssize_t sent = ::send(Fd(id), data, bytes, SendFlags(flags));
        return sent < 0 ? -Map(errno) : (int)sent;
    }
    int SendTo(int id, const void *data, U bytes, int flags, U address, U port) override
    {
        if (!online && !Local(address)) return (int)bytes;   // lost on the wire
        const sockaddr_in in = Address(address, port);
        const ssize_t sent = ::sendto(Fd(id), data, bytes, SendFlags(flags), (const sockaddr *)&in, sizeof in);
        return sent < 0 ? -Map(errno) : (int)sent;
    }
    int Receive(int id, void *data, U bytes, int flags, U *address, U *port) override
    {
        if (Cut(id))
            return Held_(id).waiting || Held_(id).timed_out ? -NetHost::NotConnected : -NetHost::WouldBlock;
        if (!online) Drop(id);
        sockaddr_in in{};
        socklen_t length = sizeof in;
        int host_flags = 0;
        if (flags & NetHost::ReceiveOutOfBand) host_flags |= MSG_OOB;
        if (flags & NetHost::ReceivePeek) host_flags |= MSG_PEEK;
        const ssize_t got = ::recvfrom(Fd(id), data, bytes, host_flags, (sockaddr *)&in, &length);
        if (got < 0) return -Map(errno);
        if (address && port)
        {
            *address = ntohl(in.sin_addr.s_addr);
            *port = ntohs(in.sin_port);
        }
        return (int)got;
    }
    int Shutdown(int id, int how) override
    {
        return Result(::shutdown(Fd(id), how == 0 ? SHUT_RD : how == 1 ? SHUT_WR : SHUT_RDWR));
    }
    int Name(int id, bool peer, U &address, U &port) override
    {
        sockaddr_in in{};
        socklen_t length = sizeof in;
        const int r = peer ? ::getpeername(Fd(id), (sockaddr *)&in, &length)
                           : ::getsockname(Fd(id), (sockaddr *)&in, &length);
        if (r < 0) return -Map(errno);
        address = ntohl(in.sin_addr.s_addr);
        port = ntohs(in.sin_port);
        return 0;
    }
    int SetOption(int id, NetHost::Option option, int value) override
    {
        const int fd = Fd(id);
        switch (option)
        {
        case NetHost::Linger:
        {
            linger l{};
            l.l_onoff = value >= 0;
            l.l_linger = value >= 0 ? value : 0;
            return Result(setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof l));
        }
        case NetHost::PendingError: return -NetHost::Invalid;
        case NetHost::MulticastInterface:
        {
            in_addr address{htonl((U)value)};
            return Result(setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &address, sizeof address));
        }
        case NetHost::MulticastTtl: case NetHost::MulticastLoop:
        {
            // BSD stores both as one byte; a byte is what every host takes.
            const unsigned char byte = (unsigned char)value;
            return Result(setsockopt(fd, IPPROTO_IP, option == NetHost::MulticastTtl ? IP_MULTICAST_TTL
                                                                                    : IP_MULTICAST_LOOP,
                                     &byte, sizeof byte));
        }
        default:
            break;
        }
        int level = 0, name = 0;
        if (!Lookup(option, level, name)) return -NetHost::NotSupported;
        return Result(setsockopt(fd, level, name, &value, sizeof value));
    }
    int Membership(int id, bool join, U group, U local) override
    {
        ip_mreq request{};
        request.imr_multiaddr.s_addr = htonl(group);
        request.imr_interface.s_addr = htonl(local);
        return Result(setsockopt(Fd(id), IPPROTO_IP, join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP,
                                 &request, sizeof request));
    }
    int GetOption(int id, NetHost::Option option, int &value) override
    {
        const int fd = Fd(id);
        if (option == NetHost::PendingError && id >= 0 && (size_t)id < held.size() && held[id].timed_out)
        {
            value = NetHost::TimedOut;
            return 0;
        }
        if (option == NetHost::PendingError)
        {
            int error = 0;
            socklen_t length = sizeof error;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) return -Map(errno);
            value = error ? Map(error) : 0;
            return 0;
        }
        if (option == NetHost::Linger)
        {
            linger l{};
            socklen_t length = sizeof l;
            if (getsockopt(fd, SOL_SOCKET, SO_LINGER, &l, &length) < 0) return -Map(errno);
            value = l.l_onoff ? l.l_linger : -1;
            return 0;
        }
        if (option == NetHost::MulticastTtl || option == NetHost::MulticastLoop)
        {
            unsigned char byte = 0;
            socklen_t length = sizeof byte;
            const int r = getsockopt(fd, IPPROTO_IP, option == NetHost::MulticastTtl ? IP_MULTICAST_TTL
                                                                                     : IP_MULTICAST_LOOP,
                                     &byte, &length);
            value = byte;
            return Result(r);
        }
        if (option == NetHost::MulticastInterface)
        {
            in_addr address{};
            socklen_t length = sizeof address;
            const int r = getsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &address, &length);
            value = (int)ntohl(address.s_addr);
            return Result(r);
        }
        int level = 0, name = 0;
        if (!Lookup(option, level, name)) return -NetHost::NotSupported;
        socklen_t length = sizeof value;
        value = 0;
        return Result(getsockopt(fd, level, name, &value, &length));
    }
    int Pending(int id) override
    {
        int bytes = 0;
        if (ioctl(Fd(id), FIONREAD, &bytes) < 0) return -Map(errno);
        return bytes;
    }
    int AtMark(int id) override
    {
        const int r = sockatmark(Fd(id));
        return r < 0 ? -Map(errno) : r;
    }
    U Poll(int id) override
    {
        const int fd = Fd(id);
        if (fd < 0) return NetHost::Failed;
        if (id >= 0 && (size_t)id < held.size())
        {
            auto &h = held[id];
            if (h.waiting)
            {
                // The cable is back before the attempt gave up: the SYN
                // that is sent again now gets its answer.
                if (online)
                {
                    h.waiting = false;
                    const sockaddr_in in = Address(h.peer, h.peer_port);
                    if (::connect(fd, (const sockaddr *)&in, sizeof in) < 0 && errno != EINPROGRESS)
                        return NetHost::Failed | NetHost::Writable;
                }
                else if (std::chrono::steady_clock::now() < h.deadline) return 0;
                else
                {
                    h.waiting = false;
                    h.timed_out = true;
                }
            }
            if (h.timed_out) return NetHost::Failed | NetHost::Writable;
            if (Cut(id)) return 0;
            if (!online) Drop(id);
        }
        pollfd p{fd, POLLIN | POLLOUT | POLLPRI, 0};
        if (::poll(&p, 1, 0) <= 0) return 0;
        U bits = 0;
        if (p.revents & POLLIN) bits |= NetHost::Readable;
        if (p.revents & POLLOUT) bits |= NetHost::Writable;
        // XNU reports POLLPRI whenever the receive pointer sits at the
        // (empty) urgent mark; only urgent data that can be peeked counts.
        if (p.revents & POLLPRI)
        {
            char urgent;
            if (::recv(fd, &urgent, 1, MSG_OOB | MSG_PEEK) == 1) bits |= NetHost::Exception;
        }
        if (p.revents & POLLHUP) bits |= NetHost::HungUp | NetHost::Readable;
        if (p.revents & (POLLERR | POLLNVAL)) bits |= NetHost::Failed;
        return bits;
    }
    void Wait(U milliseconds) override
    {
        std::vector<pollfd> set;
        for (int fd : fds)
            if (fd >= 0) set.push_back(pollfd{fd, POLLIN | POLLPRI, 0});
        if (set.empty())
        {
            usleep(milliseconds * 1000);
            return;
        }
        ::poll(set.data(), (nfds_t)set.size(), (int)milliseconds);
    }
    int Query(const char *name, U address) override
    {
        std::lock_guard<std::mutex> hold(lock);
        int ticket = -1;
        for (size_t i = 0; i < lookups.size(); ++i)
            if (!lookups[i].used) { ticket = (int)i; break; }
        if (ticket < 0)
        {
            if (lookups.size() >= 64) return -1;
            lookups.emplace_back();
            ticket = (int)lookups.size() - 1;
        }
        auto &slot = lookups[ticket];
        slot = Lookup_{};
        slot.used = true;
        auto shared = std::make_shared<Answered>();
        slot.result = shared;
        const std::string text = name ? name : "";
        const bool reverse = !name;
        // getaddrinfo blocks; the lookup runs beside the guest.
        std::thread([shared, text, reverse, address] {
            Answered r;
            if (reverse) r.found = Reverse(address, r.name, sizeof r.name);
            else r.found = Forward(text.c_str(), r);
            std::lock_guard<std::mutex> hold(shared->lock);
            shared->count = r.count;
            memcpy(shared->addresses, r.addresses, sizeof r.addresses);
            memcpy(shared->name, r.name, sizeof r.name);
            shared->found = r.found;
            shared->done = true;
        }).detach();
        return ticket;
    }
    int Answer(int ticket, U *out, U capacity, U &count, char *text, U size) override
    {
        std::lock_guard<std::mutex> hold(lock);
        count = 0;
        if (ticket < 0 || (size_t)ticket >= lookups.size() || !lookups[ticket].used) return -1;
        auto shared = lookups[ticket].result;
        std::lock_guard<std::mutex> inner(shared->lock);
        if (!shared->done) return 0;
        lookups[ticket].used = false;
        lookups[ticket].result.reset();
        if (!shared->found) return -1;
        for (U i = 0; i < shared->count && i < capacity; ++i) out[count++] = shared->addresses[i];
        if (text && size)
        {
            strncpy(text, shared->name, size - 1);
            text[size - 1] = 0;
        }
        return 1;
    }
    U Interfaces(NetHost::Interface *out, U capacity) override
    {
        ifaddrs *list = nullptr;
        if (getifaddrs(&list)) return 0;
        U count = 0;
        for (ifaddrs *p = list; p && count < capacity; p = p->ifa_next)
        {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || !(p->ifa_flags & IFF_UP)) continue;
            auto &entry = out[count++];
            entry = NetHost::Interface{};
            entry.address = ntohl(((const sockaddr_in *)p->ifa_addr)->sin_addr.s_addr);
            if (p->ifa_netmask) entry.mask = ntohl(((const sockaddr_in *)p->ifa_netmask)->sin_addr.s_addr);
            entry.broadcast = (entry.address & entry.mask) | ~entry.mask;
            entry.loopback = (p->ifa_flags & IFF_LOOPBACK) != 0;
            strncpy(entry.name, p->ifa_name, sizeof entry.name - 1);
        }
#if defined(__APPLE__)
        for (ifaddrs *p = list; p; p = p->ifa_next)
        {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_LINK) continue;
            const auto *link = (const sockaddr_dl *)p->ifa_addr;
            if (link->sdl_alen != 6) continue;
            const auto *data = (const if_data *)p->ifa_data;
            for (U i = 0; i < count; ++i)
            {
                if (strcmp(out[i].name, p->ifa_name)) continue;
                memcpy(out[i].hardware, LLADDR(link), 6);
                if (!data) continue;
                auto &e = out[i];
                e.in_octets = U(data->ifi_ibytes); e.out_octets = U(data->ifi_obytes);
                e.in_other = U(data->ifi_imcasts); e.out_other = U(data->ifi_omcasts);
                e.in_unicast = U(data->ifi_ipackets - data->ifi_imcasts);
                e.out_unicast = U(data->ifi_opackets - data->ifi_omcasts);
                e.in_errors = U(data->ifi_ierrors); e.out_errors = U(data->ifi_oerrors);
                e.in_discards = U(data->ifi_iqdrops); e.in_unknown = U(data->ifi_noproto);
            }
        }
#elif defined(__linux__)
        for (ifaddrs *p = list; p; p = p->ifa_next)
        {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_PACKET || !p->ifa_data) continue;
            const auto *data = (const rtnl_link_stats *)p->ifa_data;
            for (U i = 0; i < count; ++i)
            {
                if (strcmp(out[i].name, p->ifa_name)) continue;
                auto &e = out[i];
                e.in_octets = data->rx_bytes; e.out_octets = data->tx_bytes;
                e.in_other = data->multicast;
                e.in_unicast = data->rx_packets - data->multicast; e.out_unicast = data->tx_packets;
                e.in_errors = data->rx_errors; e.out_errors = data->tx_errors;
                e.in_discards = data->rx_dropped; e.out_discards = data->tx_dropped;
            }
        }
#endif
        freeifaddrs(list);
#if defined(__APPLE__)
        // The default gateway: the IPv4 route to 0.0.0.0/0 with a gateway, out
        // of the routing table (sysctl NET_RT_FLAGS, RTF_GATEWAY).
        int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY};
        size_t length = 0;
        if (!sysctl(mib, 6, nullptr, &length, nullptr, 0) && length)
        {
            std::vector<char> table(length);
            if (!sysctl(mib, 6, table.data(), &length, nullptr, 0))
            {
                auto round = [](size_t a) { return a > 0 ? 1 + ((a - 1) | (sizeof(uint32_t) - 1)) : sizeof(uint32_t); };
                for (size_t at = 0; at + sizeof(rt_msghdr) <= length;)
                {
                    const auto *message = (const rt_msghdr *)(table.data() + at);
                    if (!message->rtm_msglen) break;
                    at += message->rtm_msglen;
                    if ((message->rtm_addrs & (RTA_DST | RTA_GATEWAY)) != (RTA_DST | RTA_GATEWAY)) continue;
                    const auto *destination = (const sockaddr *)(message + 1);
                    const auto *gateway = (const sockaddr *)((const char *)destination + round(destination->sa_len));
                    if (destination->sa_family != AF_INET || gateway->sa_family != AF_INET) continue;
                    if (((const sockaddr_in *)destination)->sin_addr.s_addr != 0) continue;
                    if (message->rtm_addrs & RTA_NETMASK)
                    {
                        const auto *mask = (const sockaddr *)((const char *)gateway + round(gateway->sa_len));
                        if (mask->sa_len >= 8 && ((const sockaddr_in *)mask)->sin_addr.s_addr != 0) continue;
                    }
                    char name[IF_NAMESIZE] = {};
                    if (!if_indextoname(message->rtm_index, name)) continue;
                    const U next = ntohl(((const sockaddr_in *)gateway)->sin_addr.s_addr);
                    for (U i = 0; i < count; ++i)
                        if (!strcmp(out[i].name, name) && !out[i].gateway) out[i].gateway = next;
                }
            }
        }
#endif
        return count;
    }
    bool HostName(char *name, U size) override
    {
        if (gethostname(name, size)) return false;
        name[size - 1] = 0;
        return true;
    }
    U ListenAddress(U address) override { return address ? address : listen_address; }

  private:
    struct Answered
    {
        std::mutex lock;
        bool done = false, found = false;
        U addresses[16] = {}, count = 0;
        char name[256] = {};
    };
    struct Lookup_
    {
        bool used = false;
        std::shared_ptr<Answered> result;
    };
    std::mutex lock;
    std::vector<Lookup_> lookups;
    std::vector<int> fds;

    static bool Forward(const char *name, Answered &r)
    {
        // RFC 6761 reserves localhost and its subdomains for loopback. The
        // Linux resolver does not resolve every subdomain itself.
        std::string local = name;
        for (char &c : local) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (!local.empty() && local.back() == '.') local.pop_back();
        if (local == "localhost" || (local.size() > 10 && local.compare(local.size() - 10, 10, ".localhost") == 0))
        {
            strncpy(r.name, name, sizeof r.name - 1);
            r.addresses[0] = 0x7f000001; r.count = 1; return true;
        }
        addrinfo hints{}, *list = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_CANONNAME;
        if (getaddrinfo(name, nullptr, &hints, &list) || !list) return false;
        strncpy(r.name, list->ai_canonname ? list->ai_canonname : name, sizeof r.name - 1);
        for (addrinfo *p = list; p && r.count < 16; p = p->ai_next)
        {
            if (p->ai_family != AF_INET) continue;
            const U address = ntohl(((const sockaddr_in *)p->ai_addr)->sin_addr.s_addr);
            bool seen = false;
            for (U i = 0; i < r.count; ++i) seen |= r.addresses[i] == address;
            if (!seen) r.addresses[r.count++] = address;
        }
        freeaddrinfo(list);
        return r.count != 0;
    }
    static bool Reverse(U address, char *name, U size)
    {
        const sockaddr_in in = Address(address, 0);
        char host[NI_MAXHOST] = {};
        if (getnameinfo((const sockaddr *)&in, sizeof in, host, sizeof host, nullptr, 0, NI_NAMEREQD)) return false;
        strncpy(name, host, size - 1);
        name[size - 1] = 0;
        return true;
    }
    U listen_address = 0x7f000001;
    // The cable, and what each socket has to do with it.
    std::atomic<bool> online{true};
    std::vector<U> own;   // the host's addresses when the cable was pulled
    struct Held
    {
        bool stream = false;
        U peer = 0, peer_port = 0;   // 0: not connected
        bool waiting = false;        // a connection attempt into the void
        bool timed_out = false;
        std::chrono::steady_clock::time_point deadline;
    };
    std::vector<Held> held;
    const Held &Held_(int id) const { return held[id]; }
    bool Local(U address)
    {
        if ((address >> 24) == 127 || address == 0) return true;
        std::lock_guard<std::mutex> hold(lock);
        for (U a : own)
            if (a == address) return true;
        return false;
    }
    // A socket whose traffic the missing cable holds: a stream to the
    // network, connected or on its way there.
    bool Cut(int id)
    {
        if (id < 0 || (size_t)id >= held.size()) return false;
        const auto &h = held[id];
        if (h.waiting || h.timed_out) return true;
        return !online && h.stream && h.peer && !Local(h.peer);
    }
    // Datagrams from the network that arrived without the cable are dropped
    // before anyone sees them.
    void Drop(int id)
    {
        if (id < 0 || (size_t)id >= held.size() || held[id].stream) return;
        const int fd = Fd(id);
        char buffer[65536];
        for (int n = 0; n < 256; ++n)
        {
            sockaddr_in in{};
            socklen_t length = sizeof in;
            if (::recvfrom(fd, buffer, 1, MSG_PEEK, (sockaddr *)&in, &length) < 0) return;
            if (Local(ntohl(in.sin_addr.s_addr))) return;
            ::recv(fd, buffer, sizeof buffer, 0);
        }
    }

    int Keep(int fd)
    {
        for (size_t i = 0; i < fds.size(); ++i)
            if (fds[i] < 0)
            {
                fds[i] = fd;
                held[i] = Held{};
                return (int)i;
            }
        fds.push_back(fd);
        held.push_back(Held{});
        return (int)fds.size() - 1;
    }
    int Fd(int id) const { return id >= 0 && (size_t)id < fds.size() ? fds[id] : -1; }
    static sockaddr_in Address(U address, U port)
    {
        sockaddr_in in{};
#if defined(__APPLE__)
        in.sin_len = sizeof in;
#endif
        in.sin_family = AF_INET;
        in.sin_addr.s_addr = htonl(address);
        in.sin_port = htons((unsigned short)port);
        return in;
    }
    static int SendFlags(int flags)
    {
        int out = 0;
#if defined(MSG_NOSIGNAL)
        out |= MSG_NOSIGNAL;
#endif
        if (flags & NetHost::SendOutOfBand) out |= MSG_OOB;
        if (flags & NetHost::SendDontRoute) out |= MSG_DONTROUTE;
        return out;
    }
    static bool Lookup(NetHost::Option option, int &level, int &name)
    {
        level = SOL_SOCKET;
        switch (option)
        {
        case NetHost::ReuseAddress: name = SO_REUSEADDR; return true;
#if defined(SO_REUSEPORT)
        case NetHost::SharePort: name = SO_REUSEPORT; return true;
#endif
        case NetHost::KeepAlive: name = SO_KEEPALIVE; return true;
        case NetHost::Broadcast: name = SO_BROADCAST; return true;
        case NetHost::ReceiveBuffer: name = SO_RCVBUF; return true;
        case NetHost::SendBuffer: name = SO_SNDBUF; return true;
        case NetHost::OutOfBandInline: name = SO_OOBINLINE; return true;
        case NetHost::DontRoute: name = SO_DONTROUTE; return true;
        case NetHost::NoDelay: level = IPPROTO_TCP; name = TCP_NODELAY; return true;
        case NetHost::MulticastTtl: level = IPPROTO_IP; name = IP_MULTICAST_TTL; return true;
        case NetHost::Ttl: level = IPPROTO_IP; name = IP_TTL; return true;
        case NetHost::TypeOfService: level = IPPROTO_IP; name = IP_TOS; return true;
        case NetHost::MulticastLoop: level = IPPROTO_IP; name = IP_MULTICAST_LOOP; return true;
#if defined(IP_DONTFRAG)
        case NetHost::DontFragment: level = IPPROTO_IP; name = IP_DONTFRAG; return true;
#endif
        default: return false;
        }
    }
    static int Result(int r) { return r < 0 ? -Map(errno) : 0; }
    static int Map(int e)
    {
        switch (e)
        {
        case EWOULDBLOCK: return NetHost::WouldBlock;
        case EINPROGRESS: return NetHost::InProgress;
        case EALREADY: return NetHost::Already;
        case ENOTCONN: return NetHost::NotConnected;
        case EISCONN: return NetHost::IsConnected;
        case ECONNREFUSED: return NetHost::ConnectionRefused;
        case ECONNRESET: return NetHost::ConnectionReset;
        case ECONNABORTED: return NetHost::ConnectionAborted;
        case ETIMEDOUT: return NetHost::TimedOut;
        case ENETUNREACH: case ENETDOWN: return NetHost::NetworkUnreachable;
        case EHOSTUNREACH: case EHOSTDOWN: return NetHost::HostUnreachable;
        case EADDRINUSE: return NetHost::AddressInUse;
        case EADDRNOTAVAIL: return NetHost::AddressNotAvailable;
        case EACCES: case EPERM: return NetHost::Access;
        case EINVAL: return NetHost::Invalid;
        case EMSGSIZE: return NetHost::MessageSize;
        case ENOBUFS: case ENOMEM: return NetHost::NoBuffers;
        case EPIPE: case ESHUTDOWN: return NetHost::Shutdown;
        case EOPNOTSUPP: case EPROTONOSUPPORT: case ENOPROTOOPT: return NetHost::NotSupported;
        default: return NetHost::Other;
        }
    }
};
}
