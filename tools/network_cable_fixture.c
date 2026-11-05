/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_network_cable_guest.py: the network cable of the
   card is out (the host's menu Network, NEO98_NET_OFFLINE=1 for the probe).
   The loopback still answers, a connection to the network gets no answer
   and ends with WSAETIMEDOUT, a datagram to the network is sent and lost,
   and a name the DNS server of the lease would answer is not found. The
   command line carries the port of a TCP echo server on 127.0.0.1; every
   answer goes out as an OutputDebugStringA line "CABLE <name>=<hex>". */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API int WSAStartup(U, void *);
API U socket(int, int, int);
API int connect(U, const void *, int);
API int send(U, const void *, int, int);
API int recv(U, void *, int, int);
API int sendto(U, const void *, int, int, const void *, int);
API int closesocket(U);
API int WSAGetLastError(void);
API void *gethostbyname(const char *);

static char line[64];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    const char *head = "CABLE ";
    while (*head) line[at++] = *head++;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
struct Address { W family, port; U address; char zero[8]; };
static W Swap(U port) { return (W)(((port & 255) << 8) | ((port >> 8) & 255)); }

void mainCRTStartup(void)
{
    static char data[512];
    const char *p = GetCommandLineA();
    U port = 0;
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    while (*p >= '0' && *p <= '9') port = port * 10 + (U)(*p++ - '0');
    number("startup", (U)WSAStartup(0x0101, data));
    {
        /* The loopback does not need the cable. */
        static const char text[] = "cable";
        char buffer[16];
        struct Address a = {2, 0, 0x0100007f, {0}};
        U s = socket(2, 1, 0);
        int got = 0;
        a.port = Swap(port);
        number("loop_connect", (U)connect(s, &a, 16));
        number("loop_send", (U)send(s, text, 5, 0));
        while (got < 5)
        {
            const int r = recv(s, buffer + got, (int)sizeof buffer - got, 0);
            if (r <= 0) break;
            got += r;
        }
        number("loop_echo", (U)got);
        closesocket(s);
    }
    {
        /* 192.0.2.1 (TEST-NET-1) lies on the network. */
        struct Address a = {2, 0, 0x010200c0, {0}};
        U s = socket(2, 1, 0), start = GetTickCount();
        a.port = Swap(9);
        number("remote_connect", (U)connect(s, &a, 16));
        number("remote_error", (U)WSAGetLastError());
        number("remote_ms", GetTickCount() - start);
        closesocket(s);
        s = socket(2, 2, 0);
        number("remote_sendto", (U)sendto(s, "lost", 4, 0, &a, 16));
        closesocket(s);
    }
    {
        U start = GetTickCount();
        number("dns_found", gethostbyname("www.example.com") != 0);
        number("dns_error", (U)WSAGetLastError());
        number("dns_ms", GetTickCount() - start);
    }
    number("done", 1);
    ExitProcess(0);
}
