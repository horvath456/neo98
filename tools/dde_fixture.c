/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest fixture for the ported DDEML family. One process keeps two DDEML
   instances, a Progman-style server and a client-only instance, and runs the
   handshake the Win98 shell runs: service "Progman", topic "Progman", one
   XTYP_EXECUTE. Every failure leaves through ExitProcess with its own code. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U DdeInitializeA(U *, U, U, U);
API U DdeUninitialize(U);
API U DdeCreateStringHandleA(U, const char *, U);
API U DdeFreeStringHandle(U, U);
API U DdeKeepStringHandle(U, U);
API U DdeQueryStringA(U, U, char *, U, U);
API U DdeCmpStringHandles(U, U);
API U DdeGetLastError(U);
API U DdeNameService(U, U, U, U);
API U DdeConnect(U, U, U, U);
API U DdeDisconnect(U);
API U DdeClientTransaction(const void *, U, U, U, U, U, U, U *);
API U DdeGetData(U, void *, U, U);
API U DdeCreateDataHandle(U, const void *, U, U, U, U, U);
API U DdeAccessData(U, U *);
API U DdeUnaccessData(U);
API U DdeFreeDataHandle(U);
API U DdeConnectList(U, U, U, U, U);
API U DdeQueryNextServer(U, U);
API U DdeDisconnectList(U);
API U DdeQueryConvInfo(U, U, void *);
API U DdeSetUserHandle(U, U, U);
API U DdeAbandonTransaction(U, U, U);
API U FindWindowExA(U, U, const char *, const char *);
API U DdePostAdvise(U, U, U);
API void ExitProcess(U);

#define CP_WINANSI 1004
#define APPCLASS_STANDARD 0
#define APPCMD_CLIENTONLY 0x10
#define DNS_REGISTER 1
#define DNS_UNREGISTER 2
#define XTYP_CONNECT 0x1062
#define XTYP_CONNECT_CONFIRM 0x8072
#define XTYP_EXECUTE 0x4050
#define XTYP_DISCONNECT 0x80c2
#define QID_SYNC 0xffffffff
#define DDE_FACK 0x8000
#define DMLERR_NO_ERROR 0
#define DMLERR_DLL_NOT_INITIALIZED 0x4003
#define DMLERR_DLL_USAGE 0x4004
#define DMLERR_INVALIDPARAMETER 0x4006
#define DMLERR_NOTPROCESSED 0x4009
#define DMLERR_NO_CONV_ESTABLISHED 0x400a
#define DMLERR_UNFOUND_QUEUE_ID 0x4011

static const char kCommand[] = "[ShowGroup(\"Programs\",1)]";
static U connects, confirms, executes, disconnects;
static char received[64];
static U received_size;

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}
static U length(const char *s)
{
    U n = 0;
    while (s[n]) ++n;
    return n;
}
static int same(const char *a, const char *b)
{
    U i = 0;
    for (; a[i] && a[i] == b[i]; ++i) {}
    return a[i] == b[i];
}
static U __stdcall Server(U type, U format, U conv, U topic, U service, U data, U d1, U d2)
{
    (void)format; (void)conv; (void)topic; (void)service; (void)d1; (void)d2;
    if (type == XTYP_CONNECT) { ++connects; return 1; }
    if (type == XTYP_CONNECT_CONFIRM) { ++confirms; return 0; }
    if (type == XTYP_EXECUTE) {
        ++executes;
        received_size = DdeGetData(data, received, sizeof received, 0);
        return DDE_FACK;
    }
    if (type == XTYP_DISCONNECT) { ++disconnects; return 0; }
    return 0;
}
static U __stdcall Client(U type, U format, U conv, U hsz1, U hsz2, U data, U d1, U d2)
{
    (void)type; (void)format; (void)conv; (void)hsz1; (void)hsz2; (void)data; (void)d1; (void)d2;
    return 0;
}
void mainCRTStartup(void)
{
    U server = 0, client = 0, conv, result = 0, data, size = 0, access;
    U service, topic, client_service, client_topic, kept;
    char text[32];

    /* DDEML : the reserved argument must be zero. */
    check(DdeInitializeA(&server, (U)Server, APPCLASS_STANDARD, 1) == DMLERR_INVALIDPARAMETER, 0x10);
    check(server == 0, 0x11);
    check(DdeInitializeA(&server, (U)Server, APPCLASS_STANDARD, 0) == DMLERR_NO_ERROR, 0x12);
    check(server != 0, 0x13);
    /* the high word is the per-module counter, never zero. */
    check((server >> 16) != 0, 0x14);
    check(DdeGetLastError(server) == DMLERR_NO_ERROR, 0x15);
    /* an instance that does not pair answers DMLERR_DLL_NOT_INITIALIZED. */
    check(DdeGetLastError(server ^ 0x10000) == DMLERR_DLL_NOT_INITIALIZED, 0x16);

    /* only 0, CP_WINANSI and the keyboard code page are accepted. */
    check(DdeCreateStringHandleA(server, "Progman", 42) == 0, 0x17);
    check(DdeGetLastError(server) == DMLERR_INVALIDPARAMETER, 0x18);
    check(DdeGetLastError(server) == DMLERR_NO_ERROR, 0x19);
    service = DdeCreateStringHandleA(server, "Progman", CP_WINANSI);
    check(service != 0, 0x1a);
    topic = DdeCreateStringHandleA(server, "Progman", 0);
    /* The string handle is a global atom, so the same text is the same handle. */
    check(topic == service, 0x1b);
    check(DdeCreateStringHandleA(server, "", CP_WINANSI) == 0, 0x1c);

    check(DdeQueryStringA(server, service, text, sizeof text, CP_WINANSI) == 7, 0x1d);
    check(same(text, "Progman"), 0x1e);
    /* a null buffer with a size answers the length only. */
    check(DdeQueryStringA(server, service, 0, sizeof text, 0) == 7, 0x1f);
    /* A buffer with size zero receives the terminator and nothing else. */
    text[0] = 'x';
    check(DdeQueryStringA(server, service, text, 0, 0) == 0, 0x20);
    check(text[0] == 0, 0x21);

    check(DdeNameService(server, service, 0, DNS_REGISTER) != 0, 0x22);

    check(DdeInitializeA(&client, (U)Client, APPCMD_CLIENTONLY, 0) == DMLERR_NO_ERROR, 0x30);
    check(client != 0 && client != server, 0x31);
    /* a client-only instance cannot register a service name. */
    check(DdeNameService(client, service, 0, DNS_REGISTER) == 0, 0x32);
    check(DdeGetLastError(client) == DMLERR_DLL_USAGE, 0x33);
    /* Both instances live: one hidden top-level DMGClass window each, and a
     * DMGFrame for the server only (APPCMD_CLIENTONLY gets none). Counted
     * here, where both are known to exist; the runner only samples between
     * pump steps and can miss the moment. */
    {
        U found = 0, frames = 0, h = 0;
        while ((h = FindWindowExA(0, h, "DMGClass", 0)) != 0 && found < 16) ++found;
        h = 0;
        while ((h = FindWindowExA(0, h, "DMGFrame", 0)) != 0 && frames < 16) ++frames;
        check(found == 2, 0x3c);
        check(frames == 1, 0x3d);
    }

    client_service = DdeCreateStringHandleA(client, "Progman", CP_WINANSI);
    client_topic = DdeCreateStringHandleA(client, "Progman", CP_WINANSI);
    check(client_service != 0 && client_topic != 0, 0x34);
    check(DdeCmpStringHandles(service, client_service) == 0, 0x35);

    conv = DdeConnect(client, client_service, client_topic, 0);
    check(conv != 0, 0x36);
    check(connects == 1, 0x37);
    check(confirms == 1, 0x38);

    data = DdeClientTransaction(kCommand, length(kCommand) + 1, conv, 0, 0, XTYP_EXECUTE, 1000, &result);
    check(data != 0, 0x40);
    check(result == DDE_FACK, 0x41);
    check(executes == 1, 0x42);
    check(received_size == length(kCommand) + 1, 0x43);
    check(same(received, kCommand), 0x44);

    /* The data handle the transaction answers still carries the command. */
    access = DdeAccessData(data, &size);
    check(access != 0, 0x45);
    check(size == length(kCommand) + 1, 0x46);
    check(same((const char *)access, kCommand), 0x47);
    check(DdeUnaccessData(data) != 0, 0x48);
    check(DdeFreeDataHandle(data) != 0, 0x49);

    data = DdeCreateDataHandle(client, kCommand, length(kCommand) + 1, 0, client_topic, 1, 0);
    check(data != 0, 0x4a);
    check(DdeGetData(data, received, sizeof received, 0) == length(kCommand) + 1, 0x4b);
    check(DdeFreeDataHandle(data) != 0, 0x4c);

    /* DdeSetUserHandle and DdeQueryConvInfo: the value is kept per
       conversation and read back out of the CONVINFO. QID_SYNC (-1) is the
       only transaction a synchronous port knows. The Win32 CONVINFO is 60h
       bytes with hwnd and hwndPartner in its last two dwords. */
    {
        U info[24];
        U i;
        for (i = 0; i < 24; ++i) info[i] = 0;
        check(DdeSetUserHandle(conv, QID_SYNC, 0x1234abcd) != 0, 0x60);
        check(DdeSetUserHandle(conv, 7, 0x1234abcd) == 0, 0x61);
        check(DdeGetLastError(client) == DMLERR_UNFOUND_QUEUE_ID, 0x62);
        info[0] = 0x60;
        check(DdeQueryConvInfo(conv, QID_SYNC, info) == 0x60, 0x63);
        check(info[1] == 0x1234abcd, 0x64);
        check(info[5] == client_topic, 0x65);
        check(info[10] == 2, 0x66);            /* XST_CONNECTED */
        check(info[13] == 0x24 && info[22] != 0 && info[23] != 0 && info[22] != info[23], 0x72);
        info[0] = 4;
        check(DdeQueryConvInfo(conv, QID_SYNC, info) == 0, 0x67);
        check(DdeGetLastError(client) == DMLERR_INVALIDPARAMETER, 0x68);
        info[0] = 0x60;
        check(DdeQueryConvInfo(conv, 9, info) == 0, 0x69);
        check(DdeGetLastError(client) == DMLERR_UNFOUND_QUEUE_ID, 0x6a);
        /* Nothing is outstanding, so id 0 has nothing to abandon, QID_SYNC
           cannot be abandoned and every other id is unknown. */
        check(DdeAbandonTransaction(client, conv, 0) != 0, 0x6b);
        check(DdeAbandonTransaction(client, conv, QID_SYNC) == 0, 0x73);
        check(DdeGetLastError(client) == DMLERR_INVALIDPARAMETER, 0x74);
        check(DdeAbandonTransaction(client, conv, 3) == 0, 0x6c);
        check(DdeGetLastError(client) == DMLERR_UNFOUND_QUEUE_ID, 0x6d);
        /* The advise loop is not connected, so the server is told so instead
           of being told that nobody was notified. */
        check(DdePostAdvise(server, topic, 0) == 0, 0x6e);
        check(DdeGetLastError(server) == DMLERR_NOTPROCESSED, 0x6f);
        check(DdePostAdvise(client, topic, 0) == 0, 0x70);
        check(DdeGetLastError(client) == DMLERR_DLL_USAGE, 0x71);
    }

    check(DdeDisconnect(conv) != 0, 0x50);
    check(disconnects == 1, 0x51);
    /* A conversation that has ended answers nothing. */
    check(DdeDisconnect(conv) == 0, 0x52);

    /* DdeConnectList walks every server that answers the name; the list is
       read with DdeQueryNextServer and released with DdeDisconnectList. */
    {
        U list, first, second;
        list = DdeConnectList(client, client_service, client_topic, 0, 0);
        check(list != 0, 0x80);
        check(connects == 2, 0x81);
        first = DdeQueryNextServer(list, 0);
        check(first != 0, 0x82);
        second = DdeQueryNextServer(list, first);
        /* There is exactly one registered server, so the walk ends here. */
        check(second == 0, 0x83);
        check(DdeQueryNextServer(list, 0x4dc00000) == 0, 0x84);
        check(DdeDisconnectList(list) != 0, 0x85);
        check(DdeDisconnectList(list) == 0, 0x86);
        check(disconnects == 2, 0x87);
        /* A name no server carries answers nothing. */
        {
            U unknown = DdeCreateStringHandleA(client, "NoSuchServer", CP_WINANSI);
            check(unknown != 0, 0x88);
            check(DdeConnectList(client, unknown, client_topic, 0, 0) == 0, 0x89);
            check(DdeGetLastError(client) == DMLERR_NO_CONV_ESTABLISHED, 0x8a);
            check(DdeFreeStringHandle(client, unknown) != 0, 0x8b);
        }
    }

    kept = DdeKeepStringHandle(client, client_service);
    check(kept != 0, 0x53);
    check(DdeFreeStringHandle(client, client_service) != 0, 0x54);
    check(DdeFreeStringHandle(client, client_topic) != 0, 0x55);
    check(DdeNameService(server, service, 0, DNS_UNREGISTER) != 0, 0x56);
    check(DdeFreeStringHandle(server, service) != 0, 0x57);
    check(DdeUninitialize(client) != 0, 0x58);
    check(DdeUninitialize(server) != 0, 0x59);
    /* A released instance no longer pairs. */
    check(DdeUninitialize(server) == 0, 0x5a);
    ExitProcess(0);
}
