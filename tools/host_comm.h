// SPDX-License-Identifier: GPL-2.0-or-later
// POSIX implementation of the serial host of src/comm_host.h.
// A port has an endpoint when the environment names one: NEO98_COM1 to
// NEO98_COM4 (the host's option --com COM2=... sets the same variable).
// tcp:HOST:PORT a TCP connection the host opens, bytes as they are
// /dev/cu.NAME a serial device of the Mac, or the slave side of a pty
// The connection exists while a holder has the port open and is closed with
// it: a server that serves one client sees the port being handed from one
// program to the next. An endpoint that does not answer is a port with no
// cable; the provider looks for it again when the holder sends or reads, at
// most twice a second. What a slow endpoint does not take at once waits in
// a queue here, so a writer never blocks and nothing is lost.
// A TCP connection that stands shows CTS, DSR and carrier; a device shows its
// own lines and gets the line format, DTR, RTS and break of the holder.
//
#pragma once
#include "../src/comm_host.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

namespace HostComm
{
class Ports
{
  public:
    Ports()
    {
        for (unsigned i = 0; i < CommHost::PortCapacity; ++i)
        {
            char name[16];
            snprintf(name, sizeof name, "NEO98_COM%u", i + 1);
            if (const char *endpoint = getenv(name); endpoint && *endpoint) Attach(i, endpoint);
        }
        provider.owner = this;
        provider.Attached = [](void *o, unsigned port) { return Self(o).AttachedPort(port); };
        provider.Open = [](void *o, unsigned port, unsigned long long holder) { return Self(o).Open(port, holder); };
        provider.Close = [](void *o, unsigned port, unsigned long long holder) { Self(o).Close(port, holder); };
        provider.Read = [](void *o, unsigned port, unsigned char *out, unsigned capacity) {
            return Self(o).Read(port, out, capacity);
        };
        provider.Write = [](void *o, unsigned port, const unsigned char *data, unsigned count) {
            Self(o).Write(port, data, count);
        };
        provider.Pending = [](void *o, unsigned port) { return Self(o).Pending(port); };
        provider.Drain = [](void *o, unsigned port, unsigned milliseconds) { Self(o).Drain(port, milliseconds); };
        provider.Lines = [](void *o, unsigned port) { return Self(o).Lines(port); };
        provider.Control = [](void *o, unsigned port, bool dtr, bool rts, bool line_break) {
            Self(o).Control(port, dtr, rts, line_break);
        };
        provider.Configure = [](void *o, unsigned port, unsigned baud, unsigned data_bits, unsigned parity,
                                unsigned stop_bits) { Self(o).Configure(port, baud, data_bits, parity, stop_bits); };
    }
    ~Ports()
    {
        if (CommHost::Current() == &provider) CommHost::SetProvider(nullptr);
        for (auto &p : ports) Disconnect(p);
    }
    void Install() { CommHost::SetProvider(&provider); }
    // The endpoint of a port; an empty one detaches it. Only while nobody
    // holds the port.
    bool Attach(unsigned port, const char *endpoint)
    {
        if (port >= CommHost::PortCapacity) return false;
        std::lock_guard<std::mutex> guard(lock);
        auto &p = ports[port];
        if (p.holder) return false;
        p.endpoint = endpoint ? endpoint : "";
        return true;
    }
    std::string Endpoint(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        return port < CommHost::PortCapacity ? ports[port].endpoint : std::string();
    }
    // What the port has seen, for a log line and for the tests.
    struct Counters { unsigned long long sent = 0, received = 0, lost = 0; unsigned connections = 0; };
    Counters Seen(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        return port < CommHost::PortCapacity ? ports[port].seen : Counters();
    }
    bool Connected(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        return port < CommHost::PortCapacity && ports[port].fd >= 0;
    }

  private:
    using Clock = std::chrono::steady_clock;
    struct Port
    {
        std::string endpoint;
        unsigned long long holder = 0;
        int fd = -1;
        bool device = false;
        std::string queue;             // taken from the holder, not yet by the endpoint
        Clock::time_point tried{};     // the last look for the endpoint
        bool dtr = false, rts = false, line_break = false;
        unsigned baud = 9600, data_bits = 8, parity = 0, stop_bits = 0;
        Counters seen;
    };
    static Ports &Self(void *owner) { return *static_cast<Ports *>(owner); }
    bool AttachedPort(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        return port < CommHost::PortCapacity && !ports[port].endpoint.empty();
    }
    bool Open(unsigned port, unsigned long long holder)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity || ports[port].endpoint.empty()) return false;
        auto &p = ports[port];
        if (p.holder && p.holder != holder) return false;
        if (p.holder == holder) return true;
        p.holder = holder;
        p.queue.clear();
        p.tried = {};
        Connect(p);
        return true;
    }
    void Close(unsigned port, unsigned long long holder)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity || !ports[port].holder || ports[port].holder != holder) return;
        auto &p = ports[port];
        // The last line of a program must arrive before the next program's
        // first one: the endpoint gets a second to take it.
        DrainLocked(p, 1000);
        Disconnect(p);
        p.queue.clear();
        p.holder = 0;
    }
    unsigned Read(unsigned port, unsigned char *out, unsigned capacity)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity || !ports[port].holder) return 0;
        auto &p = ports[port];
        if (p.fd < 0) Connect(p);
        if (p.fd < 0 || !capacity) return 0;
        Flush(p);
        if (p.fd < 0) return 0;
        const ssize_t n = read(p.fd, out, capacity);
        if (n > 0)
        {
            p.seen.received += (unsigned long long)n;
            return (unsigned)n;
        }
        // A stream that has ended is the cable pulled; so is a broken one.
        if (n == 0 ? !p.device : (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) Disconnect(p);
        return 0;
    }
    void Write(unsigned port, const unsigned char *data, unsigned count)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity || !ports[port].holder) return;
        auto &p = ports[port];
        if (p.fd < 0) Connect(p);
        if (p.fd < 0)
        {
            p.seen.lost += count; // shifted out onto a line nobody listens on
            return;
        }
        p.queue.append(reinterpret_cast<const char *>(data), count);
        Flush(p);
    }
    unsigned Pending(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity) return 0;
        auto &p = ports[port];
        if (p.fd >= 0) Flush(p);
        return (unsigned)p.queue.size();
    }
    void Drain(unsigned port, unsigned milliseconds)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port < CommHost::PortCapacity) DrainLocked(ports[port], milliseconds);
    }
    unsigned Lines(unsigned port)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity || ports[port].fd < 0) return 0;
        auto &p = ports[port];
        if (!p.device) return CommHost::Cts | CommHost::Dsr | CommHost::Carrier;
        int bits = 0;
        // A pty has no lines; it counts as a cable with everything asserted.
        if (ioctl(p.fd, TIOCMGET, &bits) < 0) return CommHost::Cts | CommHost::Dsr | CommHost::Carrier;
        return (bits & TIOCM_CTS ? CommHost::Cts : 0u) | (bits & TIOCM_DSR ? CommHost::Dsr : 0u) |
               (bits & TIOCM_RI ? CommHost::Ring : 0u) | (bits & TIOCM_CD ? CommHost::Carrier : 0u);
    }
    void Control(unsigned port, bool dtr, bool rts, bool line_break)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity) return;
        auto &p = ports[port];
        const bool break_changed = p.line_break != line_break;
        p.dtr = dtr; p.rts = rts; p.line_break = line_break;
        if (p.fd >= 0 && p.device) ApplyLines(p, break_changed);
    }
    void Configure(unsigned port, unsigned baud, unsigned data_bits, unsigned parity, unsigned stop_bits)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (port >= CommHost::PortCapacity) return;
        auto &p = ports[port];
        p.baud = baud; p.data_bits = data_bits; p.parity = parity; p.stop_bits = stop_bits;
        if (p.fd >= 0 && p.device) ApplyFormat(p);
    }

    void Disconnect(Port &p)
    {
        if (p.fd >= 0) close(p.fd);
        p.fd = -1;
        p.queue.clear();
    }
    // Looks for the endpoint, at most twice a second.
    void Connect(Port &p)
    {
        const auto now = Clock::now();
        if (p.tried != Clock::time_point{} && now - p.tried < std::chrono::milliseconds(500)) return;
        p.tried = now;
        if (!p.endpoint.compare(0, 4, "tcp:")) ConnectTcp(p);
        else ConnectDevice(p);
        if (p.fd >= 0) ++p.seen.connections;
    }
    void ConnectTcp(Port &p)
    {
        const std::string rest = p.endpoint.substr(4);
        const auto colon = rest.rfind(':');
        if (colon == std::string::npos) return;
        const std::string host = rest.substr(0, colon), service = rest.substr(colon + 1);
        addrinfo hints{}, *found = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) || !found) return;
        for (addrinfo *a = found; a && p.fd < 0; a = a->ai_next)
        {
            const int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (fd < 0) continue;
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            int on = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
#if defined(SO_NOSIGPIPE)
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            bool connected = connect(fd, a->ai_addr, a->ai_addrlen) == 0;
            if (!connected && errno == EINPROGRESS)
            {
                // The endpoint is a program on this Mac or on its network; a
                // quarter of a second is enough for it to answer.
                pollfd wait{fd, POLLOUT, 0};
                int error = 0;
                socklen_t size = sizeof error;
                connected = poll(&wait, 1, 250) == 1 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error;
            }
            if (connected) p.fd = fd, p.device = false;
            else close(fd);
        }
        freeaddrinfo(found);
    }
    void ConnectDevice(Port &p)
    {
        const int fd = open(p.endpoint.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return;
        p.fd = fd;
        p.device = true;
        ApplyFormat(p);
        ApplyLines(p, p.line_break);
    }
    static speed_t Speed(unsigned baud)
    {
        switch (baud)
        {
        case 300: return B300; case 1200: return B1200; case 2400: return B2400; case 4800: return B4800;
        case 9600: return B9600; case 19200: return B19200; case 38400: return B38400; case 57600: return B57600;
        case 115200: return B115200; case 230400: return B230400;
        }
        return (speed_t)baud; // macOS takes the rate itself as the constant
    }
    void ApplyFormat(Port &p)
    {
        termios t{};
        if (tcgetattr(p.fd, &t) < 0) return;
        cfmakeraw(&t);
        t.c_cflag |= CLOCAL | CREAD;
        t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
        t.c_cflag |= p.data_bits == 5 ? CS5 : p.data_bits == 6 ? CS6 : p.data_bits == 7 ? CS7 : CS8;
        if (p.parity == 1) t.c_cflag |= PARENB | PARODD;
        else if (p.parity) t.c_cflag |= PARENB;
        if (p.stop_bits == 2) t.c_cflag |= CSTOPB;
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        cfsetspeed(&t, Speed(p.baud));
        tcsetattr(p.fd, TCSANOW, &t);
    }
    void ApplyLines(Port &p, bool break_changed)
    {
        int set = (p.dtr ? TIOCM_DTR : 0) | (p.rts ? TIOCM_RTS : 0), clear = (TIOCM_DTR | TIOCM_RTS) & ~set;
        ioctl(p.fd, TIOCMBIS, &set);
        ioctl(p.fd, TIOCMBIC, &clear);
        if (break_changed) ioctl(p.fd, p.line_break ? TIOCSBRK : TIOCCBRK);
    }
    // Hands the endpoint what it takes now.
    void Flush(Port &p)
    {
        while (p.fd >= 0 && !p.queue.empty())
        {
#if defined(MSG_NOSIGNAL)
            const ssize_t n = p.device ? write(p.fd, p.queue.data(), p.queue.size())
                                       : send(p.fd, p.queue.data(), p.queue.size(), MSG_NOSIGNAL);
#else
            const ssize_t n = write(p.fd, p.queue.data(), p.queue.size());
#endif
            if (n > 0)
            {
                p.seen.sent += (unsigned long long)n;
                p.queue.erase(0, (size_t)n);
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            p.seen.lost += p.queue.size();
            Disconnect(p);
        }
    }
    void DrainLocked(Port &p, unsigned milliseconds)
    {
        const auto end = Clock::now() + std::chrono::milliseconds(milliseconds);
        for (Flush(p); p.fd >= 0 && !p.queue.empty(); Flush(p))
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
            if (left <= 0) return;
            pollfd wait{p.fd, POLLOUT, 0};
            poll(&wait, 1, (int)left);
        }
    }

    std::mutex lock;
    Port ports[CommHost::PortCapacity];
    CommHost::Provider provider;
};
} // namespace HostComm
