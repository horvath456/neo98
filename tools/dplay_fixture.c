/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_dplay_sessions.py: two NEO98 sessions play one
   DirectPlay session over TCP/IP. The original DPLAYX.DLL, DPWSOCKX.DLL and
   DPLAYSVR.EXE of the tree do the work; the native part is WSOCK2.VXD.

   "DPTEST host <tag>" creates a session and waits for a player and a message.
   "DPTEST join <tag>" enumerates sessions at 127.0.0.1, joins the one that
   carries the tag, creates a player and sends a message.
   Every answer goes out as "NET <name>=<hex>". */
typedef unsigned U;
typedef unsigned short W;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API void Sleep(U);
API U CoInitialize(void *);
API U DirectPlayLobbyCreateA(void *, void **, void *, void *, U);
API U DirectPlayCreate(void *, void **, void *);

typedef struct { U a; W b, c; unsigned char d[8]; } GUID;
static const GUID TcpIp = {0x36E95EE0, 0x8577, 0x11cf, {0x96, 0x0C, 0x00, 0x80, 0xC7, 0x53, 0x4E, 0x82}};
static const GUID INet = {0xc4a54da0, 0xe0af, 0x11cf, {0x9c, 0x4e, 0x00, 0xa0, 0xc9, 0x05, 0x42, 0x5e}};
static const GUID Play4A = {0x0ab1c531, 0x4745, 0x11d1, {0xa7, 0xa1, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc}};
static const GUID Lobby3A = {0x2db72491, 0x652c, 0x11d1, {0xa7, 0xa8, 0x00, 0x00, 0xf8, 0x03, 0xab, 0xfc}};
static const GUID Game = {0x41524d39, 0x3850, 0x4c41, {0x59, 0x54, 0x45, 0x53, 0x54, 0x30, 0x30, 0x31}};

typedef U(__stdcall *Method)();
#define CALL(object, slot) ((Method)((*(void ***)(object))[slot]))

struct Session
{
    U size, flags;
    GUID instance, application;
    U max_players, current_players;
    char *name, *password;
    U reserved1, reserved2, user1, user2, user3, user4;
};
struct Name { U size, flags; char *short_name, *long_name; };

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
static U same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void *play;
static char tag[32];
static struct Session found;
static U found_ok;
static char found_name[64];

static int __stdcall Sessions(const struct Session *session, U *timeout, U flags, void *context)
{
    (void)timeout; (void)context;
    if (flags & 1) return 0; /* DPESC_TIMEDOUT: stop */
    if (session->name && same(session->name, tag))
    {
        U i;
        found = *session;
        for (i = 0; session->name[i] && i < sizeof found_name - 1; ++i) found_name[i] = session->name[i];
        found_name[i] = 0;
        found.name = found_name;
        found.password = 0;
        found_ok = 1;
        return 0;
    }
    return 1;
}

void mainCRTStartup(void)
{
    const char *p = GetCommandLineA();
    void *lobby = 0;
    U host, i = 0, size = 0, r, player = 0;
    static unsigned char address[256];
    static char buffer[512];
    struct Session session = {0};
    struct Name name = {0};
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    host = p[0] == 'h';
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    while (*p && *p != ' ' && i < sizeof tag - 1) tag[i++] = *p++;
    tag[i] = 0;

    CoInitialize(0);
    r = DirectPlayLobbyCreateA(0, &lobby, 0, 0, 0);
    number("lobby_create", r);
    /* IDirectPlayLobby3A: slot 0 QueryInterface, 4 CreateAddress. */
    {
        void *lobby3 = 0;
        number("lobby_qi", CALL(lobby, 0)(lobby, &Lobby3A, &lobby3));
        size = sizeof address;
        number("create_address", CALL(lobby3, 4)(lobby3, &TcpIp, &INet, "127.0.0.1", 10, address, &size));
    }
    {
        void *first = 0;
        static const GUID none = {0, 0, 0, {0}};
        /* GUID_NULL: an object without a service provider yet; the address
           below names TCP/IP and the host, so no dialog asks for it. */
        number("play_create", DirectPlayCreate((void *)&none, &first, 0));
        if (!first) ExitProcess(1);
        number("play_qi", CALL(first, 0)(first, &Play4A, &play));
        CALL(first, 2)(first);
    }
    if (!play) ExitProcess(2);
    /* Slot 38 InitializeConnection. */
    number("connection", CALL(play, 38)(play, address, 0));

    session.size = sizeof session;
    session.application = Game;
    name.size = sizeof name;
    if (host)
    {
        U start = GetTickCount(), joined = 0, message = 0, from = 0, to = 0, bytes;
        session.flags = 0;
        session.max_players = 4;
        session.name = tag;
        /* Slot 24 Open with DPOPEN_CREATE (2). */
        number("host_open", CALL(play, 24)(play, &session, 2));
        name.short_name = "host";
        /* Slot 6 CreatePlayer(&id, name, event, data, size, flags). */
        number("host_player", CALL(play, 6)(play, &player, &name, 0, 0, 0, 0));
        number("host_ready", 1);
        while (GetTickCount() - start < 60000 && !message)
        {
            bytes = sizeof buffer;
            /* Slot 25 Receive(&from, &to, DPRECEIVE_ALL (1), data, &size). */
            r = CALL(play, 25)(play, &from, &to, 1, buffer, &bytes);
            if (r == 0)
            {
                if (from == 0) { if (*(U *)buffer == 3) joined = 1; } /* DPSYS_CREATEPLAYERORGROUP */
                else if (bytes >= 5 && same(buffer, "hello")) message = 1;
                continue;
            }
            Sleep(20);
        }
        number("host_saw_player", joined);
        number("host_saw_message", message);
    }
    else
    {
        U start = GetTickCount(), r2 = 0;
        while (!found_ok && GetTickCount() - start < 30000)
        {
            /* Slot 13 EnumSessions(desc, timeout, callback, context, flags).
               DPENUMSESSIONS_AVAILABLE (1). */
            r2 = CALL(play, 13)(play, &session, 2000, Sessions, 0, 1);
            if (!found_ok) Sleep(200);
        }
        number("join_enum", r2);
        number("join_found", found_ok);
        if (!found_ok) ExitProcess(3);
        number("join_open", CALL(play, 24)(play, &found, 1));
        name.short_name = "guest";
        number("join_player", CALL(play, 6)(play, &player, &name, 0, 0, 0, 0));
        /* Slot 26 Send(from, to DPID_ALLPLAYERS (0), flags DPSEND_GUARANTEED (1), data, size). */
        number("join_send", CALL(play, 26)(play, player, 0, 1, "hello", 6));
        Sleep(3000);
    }
    /* Slot 4 Close. */
    number("close", CALL(play, 4)(play));
    number("done", 1);
    ExitProcess(0);
}
