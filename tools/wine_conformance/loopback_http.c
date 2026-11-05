/* SPDX-License-Identifier: GPL-2.0-or-later
 * HTTP input fixture for native URLMon tests. Runs inside the guest on both
 * systems; never substitutes an implementation of a Windows API. */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CLIENTS 16
#ifdef NEO98_SOCKET_HTTP
#define HTTP_PORT 32196
#define BODY_SIZE 512
#else
#define HTTP_PORT 32198
#define BODY_SIZE 8192
#endif
static SOCKET listeners[2] = {INVALID_SOCKET, INVALID_SOCKET};
static HANDLE worker;
static volatile LONG stopping, failed;
static unsigned requests[2];
struct client { SOCKET socket; unsigned used, short_response; char request[8192]; };

static int send_all(SOCKET socket, const char *bytes, unsigned size)
{
    while (size) {
        int sent = send(socket, bytes, size, 0);
        if (sent <= 0) { if (!sent) WSASetLastError(WSAECONNRESET); return 0; }
        bytes += sent; size -= sent;
    }
    return 1;
}

static DWORD WINAPI serve(void *unused)
{
    struct client clients[CLIENTS];
    char body[BODY_SIZE], header[256];
    unsigned i;
    (void)unused;
    memset(body, 'a', sizeof body);
    memcpy(body, "<html><body>", 12);
    memcpy(body + sizeof body - 14, "</body></html>\n", 14);
    for (i = 0; i < CLIENTS; ++i) clients[i].socket = INVALID_SOCKET;
    while (!InterlockedCompareExchange(&stopping, 0, 0)) {
        fd_set readable;
        struct timeval timeout = {0, 50000};
        int ready;
        FD_ZERO(&readable);
        FD_SET(listeners[0], &readable); FD_SET(listeners[1], &readable);
        for (i = 0; i < CLIENTS; ++i)
            if (clients[i].socket != INVALID_SOCKET) FD_SET(clients[i].socket, &readable);
        ready = select(0, &readable, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR) { InterlockedExchange(&failed, WSAGetLastError()); break; }
        for (i = 0; i < 2; ++i) if (FD_ISSET(listeners[i], &readable)) {
            SOCKET socket = accept(listeners[i], NULL, NULL);
            unsigned slot;
            if (socket == INVALID_SOCKET) { InterlockedExchange(&failed, WSAGetLastError()); break; }
            for (slot = 0; slot < CLIENTS && clients[slot].socket != INVALID_SOCKET; ++slot) {}
            if (slot == CLIENTS) { closesocket(socket); InterlockedExchange(&failed, WSAEMFILE); break; }
            clients[slot].socket = socket; clients[slot].used = 0; clients[slot].short_response = i;
        }
        for (i = 0; i < CLIENTS; ++i) if (clients[i].socket != INVALID_SOCKET && FD_ISSET(clients[i].socket, &readable)) {
            struct client *c = &clients[i];
            int count = recv(c->socket, c->request + c->used, sizeof c->request - c->used - 1, 0);
            if (count <= 0) { closesocket(c->socket); c->socket = INVALID_SOCKET; continue; }
            c->used += count; c->request[c->used] = 0;
            if (strstr(c->request, "\r\n\r\n")) {
                static const char small[] = "<html><body>native URLMon fixture</body></html>\n";
                const char *reply = c->short_response ? small : body;
                unsigned length = c->short_response ? sizeof small - 1 : sizeof body;
                int header_length;
                /* These Wine cases request GETs without request bodies. Reject
                 * unexpected traffic instead of silently changing the fixture. */
                if (strncmp(c->request, "GET /", 5)) {
                    InterlockedExchange(&failed, WSAEINVAL);
                    closesocket(c->socket); c->socket = INVALID_SOCKET; continue;
                }
                header_length = sprintf(header, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %u\r\nConnection: keep-alive\r\nCache-Control: no-cache\r\n\r\n", length);
#ifdef NEO98_SOCKET_HTTP
                /* FIONREAD's snapshot test needs one complete short reply,
                 * rather than a header followed by a separately arriving body. */
                {
                    char packet[BODY_SIZE + 256];
                    memcpy(packet, header, header_length);
                    memcpy(packet + header_length, reply, length);
                    if (!send_all(c->socket, packet, header_length + length)) {
#else
                if (!send_all(c->socket, header, header_length) || !send_all(c->socket, reply, length)) {
#endif
                    InterlockedExchange(&failed, WSAGetLastError());
                    closesocket(c->socket); c->socket = INVALID_SOCKET; continue;
                }
#ifdef NEO98_SOCKET_HTTP
                }
#endif
                ++requests[c->short_response]; c->used = 0;
            } else if (c->used == sizeof c->request - 1) {
                InterlockedExchange(&failed, WSAEMSGSIZE);
                closesocket(c->socket); c->socket = INVALID_SOCKET;
            }
        }
    }
    for (i = 0; i < CLIENTS; ++i) if (clients[i].socket != INVALID_SOCKET) closesocket(clients[i].socket);
    return 0;
}

int neo98_http_fixture_start(void)
{
    WSADATA data;
    DWORD thread_id;
    unsigned i;
    int error = WSAStartup(MAKEWORD(2, 0), &data);
    if (error) { printf("NEO98_HTTP_FIXTURE startup=%d\n", error); return 0; }
    for (i = 0; i < 2; ++i) {
        struct sockaddr_in address;
        memset(&address, 0, sizeof address);
        address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(HTTP_PORT + i);
        listeners[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listeners[i] == INVALID_SOCKET || bind(listeners[i], (struct sockaddr *)&address, sizeof address) || listen(listeners[i], 8)) {
            printf("NEO98_HTTP_FIXTURE listen=%u error=%d\n", HTTP_PORT + i, WSAGetLastError());
            goto fail;
        }
    }
    /* Win98 requires the thread-ID output pointer, unlike NT. */
    worker = CreateThread(NULL, 0, serve, NULL, 0, &thread_id);
    if (!worker) { printf("NEO98_HTTP_FIXTURE thread_error=%lu\n", GetLastError()); goto fail; }
    printf("NEO98_HTTP_FIXTURE ready ports=%u,%u address=127.0.0.1\n", HTTP_PORT, HTTP_PORT + 1);
    fflush(stdout);
    return 1;
fail:
    for (i = 0; i < 2; ++i) if (listeners[i] != INVALID_SOCKET) closesocket(listeners[i]);
    WSACleanup(); return 0;
}

int neo98_http_fixture_stop(void)
{
    DWORD status;
    InterlockedExchange(&stopping, 1);
    status = WaitForSingleObject(worker, 3000);
    printf("NEO98_HTTP_FIXTURE done requests=%u,%u error=%ld wait=%lu\n", requests[0], requests[1], failed, status);
    fflush(stdout);
    if (status != WAIT_OBJECT_0) return 0;
    CloseHandle(worker); closesocket(listeners[0]); closesocket(listeners[1]); WSACleanup();
#ifdef NEO98_SOCKET_HTTP
    return !failed && requests[0];
#else
    return !failed && requests[0] && requests[1];
#endif
}
