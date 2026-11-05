/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_network_guest.py: TCP, UDP, select,
   WSAAsyncSelect and WSAEventSelect through the original WSOCK32.DLL,
   WS2_32.DLL and MSAFD.DLL of the tree, down to the native WSOCK2.VXD.

   The command line carries three ports on 127.0.0.1: a TCP echo server, a
   UDP echo server and a port nobody listens on. Every answer goes out as an
   OutputDebugStringA line "NET <name>=<hex>", and the test compares them
   with the contract of Windows 98. Nothing here needs the
   real network. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API U GetCurrentThreadId(void);
API void Sleep(U);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);

API W RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U GetWindowThreadProcessId(U, U *);

/* WSOCK32.DLL, Winsock 1.1 */
API int WSAStartup(U, void *);
API int WSACleanup(void);
API U socket(int, int, int);
API int connect(U, const void *, int);
API int bind(U, const void *, int);
API int listen(U, int);
API U accept(U, void *, int *);
API int send(U, const void *, int, int);
API int recv(U, void *, int, int);
API int sendto(U, const void *, int, int, const void *, int);
API int recvfrom(U, void *, int, int, void *, int *);
API int closesocket(U);
API int shutdown(U, int);
API int select(int, void *, void *, void *, const void *);
API int ioctlsocket(U, int, U *);
API int getsockname(U, void *, int *);
API int getpeername(U, void *, int *);
API int setsockopt(U, int, int, const void *, int);
API int getsockopt(U, int, int, void *, int *);
API int WSAGetLastError(void);
API int WSAAsyncSelect(U, U, U, U);
API void *gethostbyname(const char *);
API void *gethostbyaddr(const void *, int, int);
API int gethostname(char *, int);
API void *getservbyname(const char *, const char *);
API U inet_addr(const char *);

/* WS2_32.DLL, Winsock 2 */
API U WSACreateEvent(void);
API int WSAEventSelect(U, U, U);
API U WSAWaitForMultipleEvents(U, const U *, int, U, int);
API int WSAEnumNetworkEvents(U, U, void *);
API int WSACloseEvent(U);
API int WSAIoctl(U, U, void *, U, void *, U, U *, void *, void *);

/* DHCPCSVC.DLL, whose requests reach VDHCP code 4 */
API U DhcpCApiInitialize(U *);
API U DhcpRequestParams(U, void *, const unsigned short *, void *, U, void *, U, void *, unsigned char *, U *, void *);
API U DhcpAcquireParameters(const unsigned short *);
API U DhcpReleaseParameters(const unsigned short *);

/* IPHLPAPI.DLL, which opens VIP in its DllMain and asks WsControl for the
   tables */
API U GetIfTable(void *, U *, int);
API U GetBestInterface(U, U *);
API U GetInterfaceInfo(void *, U *);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    const char *head = "NET ";
    while (*head) line[at++] = *head++;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
static U same(const char *a, const char *b, U n)
{
    U i;
    for (i = 0; i < n; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}
static U length(const char *s) { U n = 0; while (s[n]) ++n; return n; }

struct Address { W family, port; U address; char zero[8]; };
static W Swap(U port) { return (W)(((port & 255) << 8) | ((port >> 8) & 255)); }
static struct Address Loopback(U port)
{
    struct Address a = {2, 0, 0x0100007f, {0}};
    a.port = Swap(port);
    return a;
}
struct Set { U count; U sockets[64]; };
struct Timeval { U seconds, microseconds; };

static U ports[3];
static void ReadPorts(void)
{
    const char *p = GetCommandLineA();
    U which = 0;
    /* Skip the program name, then read three decimal numbers. */
    while (*p && *p != ' ') ++p;
    while (*p && which < 3)
    {
        while (*p == ' ') ++p;
        while (*p >= '0' && *p <= '9') ports[which] = ports[which] * 10 + (U)(*p++ - '0');
        ++which;
    }
}

/* Blocking TCP: connect, send, receive the echo. */
static void TcpEcho(void)
{
    static const char text[] = "NEO98 echo over TCP";
    char buffer[64];
    U s = socket(2, 1, 0), got = 0;
    struct Address a = Loopback(ports[0]);
    number("tcp_socket_ok", s != ~0u);
    number("tcp_connect", (U)connect(s, &a, 16));
    number("tcp_send", (U)send(s, text, sizeof text - 1, 0));
    while (got < sizeof text - 1)
    {
        const int r = recv(s, buffer + got, sizeof buffer - got, 0);
        if (r <= 0) { number("tcp_recv_error", (U)WSAGetLastError()); break; }
        got += (U)r;
    }
    number("tcp_echo", got == sizeof text - 1 && same(buffer, text, got));
    {
        struct Address local, peer;
        int n = 16, m = 16;
        number("tcp_getsockname", (U)getsockname(s, &local, &n));
        number("tcp_local_address", local.address);
        number("tcp_getpeername", (U)getpeername(s, &peer, &m));
        number("tcp_peer_port", Swap(peer.port));
    }
    number("tcp_close", (U)closesocket(s));
}

/* UDP with select: the reply arrives within five seconds. */
static void UdpEcho(void)
{
    static const char text[] = "NEO98 echo over UDP";
    char buffer[64];
    U s = socket(2, 2, 0);
    struct Address a = Loopback(ports[1]), from;
    struct Set readable;
    struct Timeval wait = {5, 0};
    int n = 16, r;
    number("udp_socket_ok", s != ~0u);
    number("udp_sendto", (U)sendto(s, text, sizeof text - 1, 0, &a, 16));
    readable.count = 1;
    readable.sockets[0] = s;
    number("udp_select", (U)select(0, &readable, 0, 0, &wait));
    r = recvfrom(s, buffer, sizeof buffer, 0, &from, &n);
    number("udp_recvfrom", (U)r);
    number("udp_echo", r == (int)sizeof text - 1 && same(buffer, text, (U)r));
    number("udp_from_port", Swap(from.port));
    number("udp_from_address", from.address);
    closesocket(s);
}

/* Nobody listens: WSAECONNREFUSED, 10061. */
static void Refused(void)
{
    U s = socket(2, 1, 0);
    struct Address a = Loopback(ports[2]);
    number("refused_connect", (U)connect(s, &a, 16));
    number("refused_error", (U)WSAGetLastError());
    closesocket(s);
}

/* Non-blocking: WSAEWOULDBLOCK on connect and on an empty receive, select
   reports the finished connect as writable. */
static void NonBlocking(void)
{
    U s = socket(2, 1, 0), on = 1;
    struct Address a = Loopback(ports[0]);
    struct Set writable;
    struct Timeval wait = {5, 0};
    char buffer[8];
    number("nb_ioctl", (U)ioctlsocket(s, 0x8004667e, &on)); /* FIONBIO */
    number("nb_connect", (U)connect(s, &a, 16));
    number("nb_connect_error", (U)WSAGetLastError());
    writable.count = 1;
    writable.sockets[0] = s;
    number("nb_select", (U)select(0, 0, &writable, 0, &wait));
    number("nb_recv", (U)recv(s, buffer, sizeof buffer, 0));
    number("nb_recv_error", (U)WSAGetLastError());
    closesocket(s);
}

/* A listener inside the guest and a client that reaches it. */
static void Listener(void)
{
    U server = socket(2, 1, 0), client = socket(2, 1, 0), peer;
    struct Address a = Loopback(0), bound, from;
    int n = 16, m = 16;
    char buffer[16];
    number("listen_bind", (U)bind(server, &a, 16));
    number("listen_listen", (U)listen(server, 5));
    number("listen_getsockname", (U)getsockname(server, &bound, &n));
    number("listen_port_set", bound.port != 0);
    number("listen_connect", (U)connect(client, &bound, 16));
    peer = accept(server, &from, &m);
    number("listen_accept_ok", peer != ~0u);
    number("listen_from_address", from.address);
    number("listen_send", (U)send(client, "guest", 5, 0));
    number("listen_recv", (U)recv(peer, buffer, sizeof buffer, 0));
    number("listen_data", same(buffer, "guest", 5));
    closesocket(peer);
    number("listen_eof", (U)recv(client, buffer, sizeof buffer, 0));
    closesocket(client);
    closesocket(server);
}

/* WSAAsyncSelect: the messages arrive on the thread that owns the window. */
#define WM_SOCKET 0x0401
static U async_socket, async_events[16], async_errors[16], async_count, async_thread, async_wparam_ok = 1;
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == WM_SOCKET)
    {
        if (async_count < 16)
        {
            async_events[async_count] = lparam & 0xffff;
            async_errors[async_count] = lparam >> 16;
            ++async_count;
        }
        if (wparam != async_socket) async_wparam_ok = 0;
        async_thread = GetCurrentThreadId();
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
static U Seen(U event)
{
    U i;
    for (i = 0; i < async_count; ++i)
        if (async_events[i] == event) return i + 1;
    return 0;
}
static void Pump(U until_event, U milliseconds)
{
    U start = GetTickCount();
    U message[7];
    while (!Seen(until_event) && GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(10);
    }
}
static void Async(void)
{
    struct { U style; void *procedure; int extra1, extra2; U instance, icon, cursor, brush; const char *menu, *name; } wc = {0};
    U window, owner;
    struct Address a = Loopback(ports[0]);
    char buffer[32];
    wc.procedure = (void *)Procedure;
    wc.name = "NEO98NetAsync";
    RegisterClassA(&wc);
    window = CreateWindowExA(0, wc.name, "net", 0, 0, 0, 10, 10, 0, 0, 0, 0);
    owner = GetWindowThreadProcessId(window, 0);
    async_socket = socket(2, 1, 0);
    number("async_select", (U)WSAAsyncSelect(async_socket, window, WM_SOCKET, 0x33)); /* READ WRITE CONNECT CLOSE */
    number("async_connect", (U)connect(async_socket, &a, 16));
    number("async_connect_error", (U)WSAGetLastError());
    Pump(0x10, 5000);
    Pump(0x02, 2000);
    number("async_connect_event", Seen(0x10) != 0);
    number("async_connect_status", Seen(0x10) ? async_errors[Seen(0x10) - 1] : ~0u);
    number("async_write_event", Seen(0x02) != 0);
    number("async_connect_before_write", Seen(0x10) && Seen(0x02) && Seen(0x10) < Seen(0x02));
    number("async_send", (U)send(async_socket, "QUIT", 4, 0));
    Pump(0x01, 5000);
    number("async_read_event", Seen(0x01) != 0);
    number("async_recv", (U)recv(async_socket, buffer, sizeof buffer, 0));
    Pump(0x20, 5000);
    number("async_close_event", Seen(0x20) != 0);
    number("async_thread_ok", async_thread == owner);
    number("async_wparam_ok", async_wparam_ok);
    closesocket(async_socket);
}

/* WSAEventSelect with WSAEnumNetworkEvents. */
static void Events(void)
{
    U s = socket(2, 1, 0), event = WSACreateEvent(), wait;
    struct Address a = Loopback(ports[0]);
    struct { U events; int errors[10]; } network;
    char buffer[32];
    number("event_create_ok", event != 0);
    number("event_select", (U)WSAEventSelect(s, event, 0x31)); /* READ CONNECT CLOSE */
    number("event_connect", (U)connect(s, &a, 16));
    number("event_connect_error", (U)WSAGetLastError());
    wait = WSAWaitForMultipleEvents(1, &event, 0, 5000, 0);
    number("event_wait_connect", wait);
    number("event_enum", (U)WSAEnumNetworkEvents(s, event, &network));
    number("event_connect_bit", network.events & 0x10);
    number("event_connect_status", (U)network.errors[4]); /* FD_CONNECT_BIT */
    send(s, "event", 5, 0);
    wait = WSAWaitForMultipleEvents(1, &event, 0, 5000, 0);
    number("event_wait_read", wait);
    WSAEnumNetworkEvents(s, event, &network);
    number("event_read_bit", network.events & 0x01);
    number("event_recv", (U)recv(s, buffer, sizeof buffer, 0));
    closesocket(s);
    WSACloseEvent(event);
}

/* Names through the resolver of the host, and the services file. */
static void Names(void)
{
    struct Host { char *name; char **aliases; short type, length; U **addresses; } *host;
    struct Service { char *name; char **aliases; W port; char *protocol; } *service;
    char name[64];
    host = (struct Host *)gethostbyname("localhost");
    number("dns_localhost_ok", host != 0);
    if (host) number("dns_localhost", *host->addresses[0]);
    else number("dns_localhost_error", (U)WSAGetLastError());
    number("dns_gethostname", (U)gethostname(name, sizeof name));
    number("dns_hostname_length", length(name));
    /* The reverse question first: RNR20 keeps the answers of forward
       lookups and would answer 127.0.0.1 from that cache afterwards. */
    {
        U loopback = 0x0100007f;
        host = (struct Host *)gethostbyaddr(&loopback, 4, 2);
        number("dns_reverse_ok", host != 0);
        if (host) number("dns_reverse_localhost", length(host->name) == 9 && same(host->name, "localhost", 9));
        else number("dns_reverse_error", (U)WSAGetLastError());
    }
    /* A name RNR20 does not answer itself: hosts file, then a DNS question
       to the server VDHCP names, which the host resolver answers. */
    host = (struct Host *)gethostbyname("neo98-test.localhost");
    number("dns_query_ok", host != 0);
    if (host) number("dns_query", *host->addresses[0]);
    else number("dns_query_error", (U)WSAGetLastError());
    /* The own name: RNR20 asks WsControl for the addresses of the stack. */
    if (!gethostname(name, sizeof name))
    {
        host = (struct Host *)gethostbyname(name);
        number("dns_self_ok", host != 0);
        if (host) number("dns_self_not_loopback", *host->addresses[0] != 0x0100007f && *host->addresses[0] != 0);
    }
    service = (struct Service *)getservbyname("ftp", "tcp");
    number("dns_service_ok", service != 0);
    if (service) number("dns_service_port", Swap(service->port));
    number("dns_inet_addr", inet_addr("10.1.2.3"));
}

/* Options: the shadow bits of WSOCK2 and the ones the provider passes on. */
static void Options(void)
{
    U s = socket(2, 1, 0), d = socket(2, 2, 0), value = 1, answer = 0;
    int n = 4;
    number("opt_reuse_set", (U)setsockopt(s, 0xffff, 4, &value, 4));
    number("opt_reuse_get", (U)getsockopt(s, 0xffff, 4, &answer, &n));
    number("opt_reuse", answer);
    n = 4; answer = 0;
    number("opt_type_get", (U)getsockopt(d, 0xffff, 0x1008, &answer, &n));
    number("opt_type", answer);
    value = 1;
    number("opt_nodelay_set", (U)setsockopt(s, 6, 1, &value, 4));
    n = 4; answer = 7;
    number("opt_error_get", (U)getsockopt(s, 0xffff, 0x1007, &answer, &n));
    number("opt_error", answer);
    /* SO_PROTOCOL_INFOA: WS2_32 asks the provider for the W form (WSOCK2
       c0003a06, the template of the socket type) and converts it; it puts
       the caller's own length back, and a short buffer gets
       WSAEFAULT with the needed 174h. */
    {
        static unsigned char info[0x174 + 16];
        n = sizeof info;
        number("opt_protocol_get", (U)getsockopt(d, 0xffff, 0x2004, info, &n));
        number("opt_protocol_size", (U)n);
        number("opt_protocol_type", *(U *)(info + 0x58));
        number("opt_protocol_protocol", *(U *)(info + 0x5c));
        number("opt_protocol_catalog", *(U *)(info + 0x24) != 0);
        number("opt_protocol_name", info[0x74] == 'M' && info[0x7f] == 'u' && info[0x80] == 'd' && info[0x81] == 'p');
        n = 16;
        number("opt_protocol_short", (U)getsockopt(s, 0xffff, 0x2004, info, &n));
        number("opt_protocol_short_error", (U)WSAGetLastError());
        number("opt_protocol_short_size", (U)n);
    }
    /* SO_BROADCAST on a stream socket is not an option there (c0003cf6). */
    number("opt_broadcast_stream", (U)setsockopt(s, 0xffff, 0x20, &value, 4));
    number("opt_broadcast_stream_error", (U)WSAGetLastError());
    /* A broadcast without SO_BROADCAST is refused with WSAEACCES. */
    {
        struct Address all = {2, 0, 0xffffffff, {0}};
        all.port = Swap(9);
        number("opt_broadcast_denied", (U)sendto(d, "x", 1, 0, &all, 16));
        number("opt_broadcast_denied_error", (U)WSAGetLastError());
    }
    /* recv on a socket that never connected: WSAENOTCONN. */
    {
        char buffer[4];
        number("opt_notconn", (U)recv(s, buffer, 4, 0));
        number("opt_notconn_error", (U)WSAGetLastError());
    }
    closesocket(s);
    closesocket(d);
    /* A closed socket is not a socket any more: WSAENOTSOCK. */
    number("opt_closed", (U)closesocket(s));
    number("opt_closed_error", (U)WSAGetLastError());
}

/* WSAAsyncSelect for a window of another thread: the messages go to the
   thread that owns the window, not to the one that asked. */
static U far_window, far_thread, far_ready, far_stop;
static U __stdcall WindowThread(void *unused)
{
    struct { U style; void *procedure; int extra1, extra2; U instance, icon, cursor, brush; const char *menu, *name; } wc = {0};
    U message[7];
    (void)unused;
    wc.procedure = (void *)Procedure;
    wc.name = "NEO98NetFar";
    RegisterClassA(&wc);
    far_window = CreateWindowExA(0, wc.name, "far", 0, 0, 0, 10, 10, 0, 0, 0, 0);
    far_thread = GetCurrentThreadId();
    far_ready = 1;
    while (!far_stop)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(5);
    }
    return 0;
}
static void FarWindow(void)
{
    U id, thread = CreateThread(0, 0, WindowThread, 0, 0, &id), start = GetTickCount();
    struct Address a = Loopback(ports[0]);
    while (!far_ready && GetTickCount() - start < 5000) Sleep(5);
    async_count = 0;
    async_thread = 0;
    async_socket = socket(2, 1, 0);
    number("far_select", (U)WSAAsyncSelect(async_socket, far_window, WM_SOCKET, 0x10));
    connect(async_socket, &a, 16);
    start = GetTickCount();
    while (!Seen(0x10) && GetTickCount() - start < 5000) Sleep(10);
    number("far_connect_event", Seen(0x10) != 0);
    number("far_thread_ok", async_thread == far_thread && async_thread != GetCurrentThreadId());
    closesocket(async_socket);
    far_stop = 1;
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
}

static U __stdcall Worker(void *unused)
{
    /* A second thread blocks in recv while the first one is free. */
    U s = socket(2, 1, 0);
    struct Address a = Loopback(ports[0]);
    char buffer[16];
    (void)unused;
    connect(s, &a, 16);
    send(s, "thread", 6, 0);
    number("thread_recv", (U)recv(s, buffer, sizeof buffer, 0));
    closesocket(s);
    return 0;
}

/* WSAIoctl: the provider requests of AFVXD prov_12 and the OVERLAPPED
   protocol of WSOCK2 ioctl_107 (c0002180). */
static void Ioctls(void)
{
    U d = socket(2, 2, 0), event = WSACreateEvent(), bytes = 0, answer = 0;
    static U overlapped[5], list[16], local[4];
    static const U loopback[4] = {2, 0x0100007f, 0, 0};
    overlapped[4] = event;
    number("ioctl_fionread", (U)WSAIoctl(d, 0x4004667f, 0, 0, &answer, 4, &bytes, overlapped, 0));
    number("ioctl_fionread_bytes", bytes);
    number("ioctl_fionread_internal", overlapped[0]);
    number("ioctl_fionread_high", overlapped[1]);
    number("ioctl_fionread_event", WSAWaitForMultipleEvents(1, &event, 0, 0, 0));
    bytes = 0;
    number("ioctl_route", (U)WSAIoctl(d, 0xc8000014, (void *)loopback, 16, local, 16, &bytes, 0, 0));
    number("ioctl_route_bytes", bytes);
    number("ioctl_route_address", local[1]);
    bytes = 0;
    number("ioctl_list", (U)WSAIoctl(d, 0x48000016, 0, 0, list, sizeof list, &bytes, 0, 0));
    number("ioctl_list_consistent", bytes == 4 + 0x18 * list[0] && (!list[0] || list[2] == 16));
    number("ioctl_list_short", (U)WSAIoctl(d, 0x48000016, 0, 0, list, 2, &bytes, 0, 0));
    number("ioctl_list_short_error", (U)WSAGetLastError());
    number("ioctl_qos", (U)WSAIoctl(d, 0x8800000b, 0, 0, 0, 0, &bytes, 0, 0));
    number("ioctl_qos_error", (U)WSAGetLastError());
    /* A change notification stays pending until the socket goes. */
    overlapped[0] = overlapped[1] = 0;
    overlapped[4] = event;
    number("ioctl_change", (U)WSAIoctl(d, 0x28000017, 0, 0, 0, 0, &bytes, overlapped, 0));
    number("ioctl_change_error", (U)WSAGetLastError());
    number("ioctl_change_internal", overlapped[0]);
    number("ioctl_change_open", WSAWaitForMultipleEvents(1, &event, 0, 0, 0));
    closesocket(d);
    number("ioctl_change_closed", WSAWaitForMultipleEvents(1, &event, 0, 2000, 0));
    number("ioctl_change_aborted", overlapped[0]);
    WSACloseEvent(event);
}

/* A raw ICMP socket: the echo request goes without IP header, the answer
   comes with one, as on Win98. */
static void RawIcmp(void)
{
    U r = socket(2, 3, 1);
    static unsigned char packet[40], answer[128];
    struct Address to = {2, 0, 0x0100007f, {0}}, from = {0};
    int fromlen = sizeof from, got, i;
    U sum = 0;
    number("raw_socket_ok", r != ~0u);
    if (r == ~0u)
    {
        number("raw_socket_error", (U)WSAGetLastError());
        goto udp_check;
    }
    packet[0] = 8; packet[4] = 0x12; packet[5] = 0x34; packet[7] = 1;
    for (i = 8; i < 40; ++i) packet[i] = (unsigned char)('a' + i % 26);
    for (i = 0; i < 40; i += 2) sum += (U)packet[i] << 8 | packet[i + 1];
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    sum = ~sum & 0xffff;
    packet[2] = (unsigned char)(sum >> 8); packet[3] = (unsigned char)sum;
    number("raw_sendto", (U)sendto(r, packet, 40, 0, &to, sizeof to));
    got = recvfrom(r, answer, sizeof answer, 0, &from, &fromlen);
    number("raw_recvfrom", (U)got);
    number("raw_ip_header", (answer[0] >> 4) == 4 && answer[9] == 1);
    number("raw_reply", answer[(answer[0] & 15) * 4] == 0 && answer[(answer[0] & 15) * 4 + 7] == 1);
    number("raw_from", from.address);
    closesocket(r);
udp_check:
    r = socket(2, 3, 17);
    number("raw_udp", r);
    number("raw_udp_error", (U)WSAGetLastError());
}

/* DhcpRequestParams asks VDHCP (code 4) for options of the lease: the DNS
   server and the mask are there, a domain name is not. Renew and release go
   the same way. */
static void Dhcp(void)
{
    static unsigned char buffer[512];
    static const unsigned short name[] = {'0', '0', '0', '0', 0};
    static struct { U flags, id, vendor; unsigned char *data; U bytes; } want[3] = {{0, 6}, {0, 1}, {0, 15}};
    U version = 0, size = sizeof buffer;
    number("dhcp_init", DhcpCApiInitialize(&version));
    number("dhcp_request", DhcpRequestParams(2, 0, name, 0, 0, 0, 3, want, buffer, &size, 0));
    number("dhcp_dns", want[0].bytes == 4 ? *(U *)want[0].data : 0);
    number("dhcp_mask_bytes", want[1].bytes);
    number("dhcp_domain_bytes", want[2].bytes);
    number("dhcp_release", DhcpReleaseParameters(name));
    number("dhcp_renew", DhcpAcquireParameters(name));
}

/* The IP Helper works only when VIP opened; SENS takes the network
   for alive when the card counts unicast packets in Windows 98. */
static void IpHelper(void)
{
    static U table[0x1000];
    static U map[0x400];
    U size = sizeof table, index = 0, i, unicast = 0, card = 0;
    number("iphlp_iftable", GetIfTable(table, &size, 0));
    number("iphlp_ifcount", table[0]);
    for (i = 0; i < table[0] && i < 2; ++i)
    {
        const U *row = table + 1 + i * (0x35c / 4);
        if (row[0x204 / 4] == 6) { card = 1; unicast = row[0x22c / 4] | row[0x244 / 4]; }
    }
    number("iphlp_card", card);
    number("iphlp_unicast", unicast != 0);
    number("iphlp_best", GetBestInterface(0x0100007f, &index));
    number("iphlp_best_index", index);
    size = sizeof map;
    number("iphlp_ifinfo", GetInterfaceInfo(map, &size));
    number("iphlp_ifinfo_count", map[0]);
    /* IP_ADAPTER_INDEX_MAP of the card: the index, then the NDIS name of the
       binding as VIP keeps it, ANSI bytes in the WCHAR field (c0001a0c). */
    number("iphlp_ifinfo_index", map[1]);
    number("iphlp_ifinfo_name", map[2]);
    number("iphlp_ifinfo_end", map[3] & 0xffff);
}

/* More sockets than the old table of 64 handles per process held. */
static void ManySockets(void)
{
    static U list[150];
    U opened = 0, i, distinct = 1;
    for (i = 0; i < 150; ++i)
    {
        list[i] = socket(2, 2, 0);
        if (list[i] != ~0u) ++opened;
    }
    for (i = 1; i < 150; ++i)
        if (list[i] == list[i - 1]) distinct = 0;
    number("many_opened", opened);
    number("many_distinct", distinct);
    for (i = 0; i < 150; ++i)
        if (list[i] != ~0u) closesocket(list[i]);
    list[0] = socket(2, 1, 0);
    number("many_after", list[0] != ~0u);
    closesocket(list[0]);
}

void mainCRTStartup(void)
{
    static char data[512];
    U thread, id;
    ReadPorts();
    number("ports", ports[0] | (ports[2] << 16));
    number("startup", (U)WSAStartup(0x101, data));
    number("startup_version", *(W *)data);
    TcpEcho();
    UdpEcho();
    Refused();
    NonBlocking();
    Listener();
    Async();
    Events();
    Names();
    Options();
    Ioctls();
    RawIcmp();
    Dhcp();
    IpHelper();
    ManySockets();
    FarWindow();
    thread = CreateThread(0, 0, Worker, 0, 0, &id);
    number("thread_wait", WaitForSingleObject(thread, 10000));
    CloseHandle(thread);
    number("cleanup", (U)WSACleanup());
    number("done", 1);
    ExitProcess(0);
}
