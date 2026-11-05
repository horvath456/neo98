/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A TCP and UDP server through the original Win98 Winsock DLLs. */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API int WSAStartup(U, void *);
API U socket(int, int, int);
API int bind(U, const void *, int);
API int listen(U, int);
API U accept(U, void *, int *);
API int recv(U, void *, int, int);
API int send(U, const void *, int, int);
API int recvfrom(U, void *, int, int, void *, int *);
API int sendto(U, const void *, int, int, const void *, int);
API int closesocket(U);
struct Address { W family, port; U address; char zero[8]; };
static W swap(U port) { return (W)((port << 8) | (port >> 8)); }
static void check(int ok)
{
    if (!ok) { OutputDebugStringA("LISTEN FAIL"); ExitProcess(1); }
}
void mainCRTStartup(void)
{
    char data[512], buffer[16];
    const char *p = GetCommandLineA();
    U ports[2] = {0}, i, tcp, udp, peer, count;
    struct Address a = {2, 0, 0, {0}}, from;
    int size = sizeof from;
    while (*p && *p != ' ') ++p;
    for (i = 0; i < 2; ++i) {
        while (*p == ' ') ++p;
        while (*p >= '0' && *p <= '9') ports[i] = ports[i] * 10 + (U)(*p++ - '0');
        check(ports[i] > 0 && ports[i] < 65536);
    }
    check(!WSAStartup(0x101, data));
    tcp = socket(2, 1, 0); udp = socket(2, 2, 0);
    check(tcp != ~0u && udp != ~0u);
    a.port = swap(ports[0]); check(!bind(tcp, &a, sizeof a));
    check(!listen(tcp, 2));
    a.port = swap(ports[1]); check(!bind(udp, &a, sizeof a));
    OutputDebugStringA("LISTEN READY");
    peer = accept(tcp, &from, &size); check(peer != ~0u);
    for (count = 0; count < 4;) {
        int got = recv(peer, buffer + count, 4 - count, 0);
        check(got > 0); count += (U)got;
    }
    for (count = 0; count < 4;) {
        int sent = send(peer, buffer + count, 4 - count, 0);
        check(sent > 0); count += (U)sent;
    }
    closesocket(peer); closesocket(tcp);
    size = sizeof from;
    check(recvfrom(udp, buffer, sizeof buffer, 0, &from, &size) == 4);
    check(sendto(udp, buffer, 4, 0, &from, size) == 4);
    closesocket(udp);
    OutputDebugStringA("LISTEN PASS"); ExitProcess(0);
}
