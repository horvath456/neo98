/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_multicast_guest.py: UDP multicast, broadcast and
   unicast through the original WS2_32.DLL, WSOCK32.DLL and MSAFD.DLL of the
   tree, down to the native WSOCK2.VXD. The pattern is the one of the
   ds-recomp games (NSMB.EXE): socket, SO_REUSEADDR, bind to the wildcard on
   the game port, IP_ADD_MEMBERSHIP on 239.255.83.68, a second socket with
   IP_MULTICAST_TTL and IP_MULTICAST_LOOP that sends to the group, and
   SO_BROADCAST to 255.255.255.255 as the fallback.

   Command line: MCAST.EXE <port> <host address> <host port>. The test
   process joins the same group on the Mac and stands for the other machines
   of the LAN; <host address>:<host port> is its unicast socket on the Mac's
   own LAN address. Every answer goes out as "MC <name>=<hex>". */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U LoadLibraryA(const char *);
API void *GetProcAddress(U, const char *);
API U GetTickCount(void);

/* WS2_32.DLL */
API int WSAStartup(U, void *);
API int WSACleanup(void);
API U socket(int, int, int);
API int bind(U, const void *, int);
API int sendto(U, const void *, int, int, const void *, int);
API int recvfrom(U, void *, int, int, void *, int *);
API int closesocket(U);
API int select(int, void *, void *, void *, const void *);
API int setsockopt(U, int, int, const void *, int);
API int connect(U, const void *, int);
API int getsockname(U, void *, int *);
API int getsockopt(U, int, int, void *, int *);
API int WSAGetLastError(void);

typedef int(__stdcall *SetOption)(U, int, int, const void *, int);

struct In { W family, port; U address; unsigned char zero[8]; };
struct Request { U group, iface; };

static char line[128];
static void number(const char *label, U value)
{
    char *p = line;
    const char *s = "MC ";
    while (*s) *p++ = *s++;
    while (*label) *p++ = *label++;
    *p++ = '=';
    for (int i = 7; i >= 0; --i) *p++ = "0123456789abcdef"[(value >> (i * 4)) & 15];
    *p++ = '\n';
    *p = 0;
    OutputDebugStringA(line);
}
static U parse(const char **p)
{
    U v = 0;
    while (**p == ' ') ++*p;
    while (**p >= '0' && **p <= '9') v = v * 10 + (U)(*(*p)++ - '0');
    return v;
}
static U parse_address(const char **p)
{
    U a = parse(p); ++*p;
    U b = parse(p); ++*p;
    U c = parse(p); ++*p;
    U d = parse(p);
    return a | b << 8 | c << 16 | d << 24;
}
static W swap(U port) { return (W)((port >> 8 & 0xff) | (port & 0xff) << 8); }
static struct In address(U ip, U port)
{
    struct In a = {2, swap(port), ip, {0}};
    return a;
}
static int same(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}
/* The next datagram on `s` that starts with `want`, within `ms`. Returns its
   length or 0; `from` gets the sender's address. */
static int wait_for(U s, const char *want, int length, U ms, U *from)
{
    const U until = GetTickCount() + ms;
    char data[64];
    while ((int)(until - GetTickCount()) > 0)
    {
        struct { U count; U socket; } set = {1, s};
        U timeout[2] = {0, 100000};
        if (select(0, &set, 0, 0, timeout) != 1) continue;
        struct In sender;
        int size = sizeof sender;
        const int r = recvfrom(s, data, sizeof data, 0, &sender, &size);
        if (r == length && same(data, want, length))
        {
            if (from) *from = sender.address;
            return r;
        }
    }
    return 0;
}
static U receiver(const char *label, U port, int ws2_join, SetOption ws1)
{
    const U s = socket(2, 2, 17);
    const U on = 1;
    number(label, s != ~0u);
    setsockopt(s, 0xffff, 4, &on, 4);
    const struct In any = address(0, port);
    number(ws2_join ? (label[0] == 'f' ? "bind" : "bind2") : "bind3", (U)bind(s, &any, sizeof any));
    const struct Request join = {0x4453ffefu, 0};   /* 239.255.83.68, INADDR_ANY */
    const int r = ws2_join ? setsockopt(s, 0, 12, &join, sizeof join) : ws1(s, 0, 5, &join, sizeof join);
    number(ws2_join ? "join" : "join_ws1", (U)r);
    if (r) number("join_error", (U)WSAGetLastError());
    return s;
}
void __stdcall mainCRTStartup(void)
{
    const char *p = GetCommandLineA();
    while (*p && *p != ' ') ++p;
    const U port = parse(&p), host = parse_address(&p), host_port = parse(&p);
    unsigned char data[512];
    number("startup", (U)WSAStartup(0x0202, data));
    const U wsock32 = LoadLibraryA("WSOCK32.DLL");
    const SetOption ws1 = (SetOption)GetProcAddress(wsock32, "setsockopt");
    number("wsock32", ws1 != 0);

    /* Two members in this process stand for two instances in the machine;
       the third joins through Winsock 1.1, whose IP_ADD_MEMBERSHIP is 5. */
    const U first = receiver("first", port, 1, ws1), second = receiver("second", port, 1, ws1);
    const U third = receiver("third", port, 0, ws1);

    /* The sender: IP_MULTICAST_TTL 1, IP_MULTICAST_LOOP 1. */
    const U s = socket(2, 2, 17);
    const U one = 1;
    number("ttl", (U)setsockopt(s, 0, 10, &one, 4));
    number("loop", (U)setsockopt(s, 0, 11, &one, 4));
    U value = 0;
    int size = 4;
    number("ttl_get", (U)getsockopt(s, 0, 10, &value, &size));
    number("ttl_value", value);
    const struct In group = address(0x4453ffefu, port);
    number("send", (U)sendto(s, "MCAST1", 6, 0, &group, sizeof group));
    number("first_got", (U)wait_for(first, "MCAST1", 6, 3000, 0));
    number("second_got", (U)wait_for(second, "MCAST1", 6, 3000, 0));
    number("third_got", (U)wait_for(third, "MCAST1", 6, 3000, 0));
    /* The Mac's member answers to the group (another machine of the LAN). */
    U from = 0;
    number("lan_got", (U)wait_for(first, "LANMC1", 6, 10000, &from));

    /* Broadcast, the games' fallback. */
    const U b = socket(2, 2, 17);
    number("broadcast_option", (U)setsockopt(b, 0xffff, 0x20, &one, 4));
    const struct In all = address(0xffffffffu, port);
    number("broadcast_send", (U)sendto(b, "BCAST1", 6, 0, &all, sizeof all));
    number("broadcast_got", (U)wait_for(second, "BCAST1", 6, 3000, 0));

    /* Unicast both ways with the Mac's LAN address; the answer comes to a
       socket of its own on the next port, which nobody else shares. */
    const U u = socket(2, 2, 17);
    const struct In next = address(0, port + 1);
    number("unicast_bind", (U)bind(u, &next, sizeof next));
    const struct In peer = address(host, host_port);
    number("unicast_send", (U)sendto(u, "UNIOUT", 6, 0, &peer, sizeof peer));
    number("unicast_got", (U)wait_for(u, "UNIIN1", 6, 10000, &from));
    number("unicast_from_host", from == host);

    /* A socket on a fixed port sends to a server beyond the loopback, the
       way a game asks its master server; 192.0.2.1 (TEST-NET-1) goes out the
       default route like any public address. Then a datagram connect to it
       names the Mac's address on that route. */
    const U f = socket(2, 2, 17);
    const struct In fixed = address(0, port + 2);
    number("fixed_bind", (U)bind(f, &fixed, sizeof fixed));
    const struct In far = address(0x010200c0u, 9);
    number("fixed_send", (U)sendto(f, "FIXED1", 6, 0, &far, sizeof far));
    const U c = socket(2, 2, 17);
    number("connect", (U)connect(c, &far, sizeof far));
    struct In me;
    int me_size = sizeof me;
    number("connect_name", (U)getsockname(c, &me, &me_size));
    number("connect_local", me.address == host);
    closesocket(f); closesocket(c);
    const struct Request leave = {0x4453ffefu, 0};
    number("leave", (U)setsockopt(first, 0, 13, &leave, sizeof leave));
    closesocket(first); closesocket(second); closesocket(third); closesocket(s); closesocket(b); closesocket(u);
    number("cleanup", (U)WSACleanup());
    number("done", 1);
    ExitProcess(0);
}
