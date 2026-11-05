// SPDX-License-Identifier: GPL-2.0-or-later
// x86 guest of tools/test_comm_host_native.py: a program that uses COM2 the
// way the launcher of the reference system and its games do, against the endpoint the
// runner serves. The command line names the part:
// talk open, exclusive open, 115200 8N1, the "return at once" timeouts,
// lines both ways, 20000 bytes each way, timed and waiting reads,
// WaitCommEvent, the modem lines, FlushFileBuffers, close and open
// again as a new connection
// absent COM1, whose endpoint does not answer: everything succeeds,
// nothing arrives
// hold open COM2, tell the endpoint, wait for its 'R', close
// leave the same, but end without closing
// refused COM2 is held by another process: the open fails with 5
// after COM2 is free again: open, one line each way, close
// Every check exits with its line number.
#define API __declspec(dllimport)
#define S __stdcall
typedef unsigned U;
API void S ExitProcess(U);
API U S GetLastError(void);
API U S CloseHandle(U);
API U S CreateFileA(const char *, U, U, U, U, U, U);
API U S ReadFile(U, void *, U, U *, U);
API U S WriteFile(U, const void *, U, U *, U);
API U S GetFileType(U);
API U S FlushFileBuffers(U);
API U S Sleep(U);
API U S GetTickCount(void);
API const char *S GetCommandLineA(void);
API U S GetCommState(U, void *);
API U S SetCommState(U, void *);
API U S SetCommTimeouts(U, void *);
API U S SetupComm(U, U, U);
API U S PurgeComm(U, U);
API U S ClearCommError(U, U *, U *);
API U S GetCommModemStatus(U, U *);
API U S EscapeCommFunction(U, U);
API U S SetCommMask(U, U);
API U S WaitCommEvent(U, U *, U);
API U S TransmitCommChar(U, U);
#define CHECK(x) do { if (!(x)) ExitProcess(__LINE__); } while (0)
void *memset(void *p, int v, unsigned n) { unsigned char *q = p; while (n--) *q++ = v; return p; }
void *memcpy(void *p, const void *s, unsigned n) { unsigned char *q = p; const unsigned char *r = s; while (n--) *q++ = *r++; return p; }
static U length(const char *s) { U n = 0; while (s[n]) ++n; return n; }
static U same(const char *a, const char *b, U n) { while (n--) if (*a++ != *b++) return 0; return 1; }
static U port;

// DCB as the launcher sets it: 115200 8N1, binary, DTR and RTS on.
static void configure(void)
{
    unsigned char dcb[28];
    U timeouts[5] = {~0u, 0, 0, 0, 100}; // ReadIntervalTimeout = MAXDWORD: return at once
    memset(dcb, 0, sizeof dcb);
    *(U *)dcb = 28;
    CHECK(GetCommState(port, dcb));
    *(U *)(dcb + 4) = 115200;
    *(U *)(dcb + 8) = 1 | 1 << 4 | 1 << 12; // fBinary, DTR_CONTROL_ENABLE, RTS_CONTROL_ENABLE
    dcb[18] = 8; dcb[19] = 0; dcb[20] = 0;
    CHECK(SetCommState(port, dcb));
    CHECK(SetCommTimeouts(port, timeouts));
}
static void send(const char *text)
{
    U written = 0;
    CHECK(WriteFile(port, text, length(text), &written, 0) && written == length(text));
}
// A line of the endpoint, read the way the launcher drains the port.
static U line(char *out, U capacity, U milliseconds)
{
    U count = 0;
    const U end = GetTickCount() + milliseconds;
    for (;;)
    {
        char c;
        U got = 0;
        CHECK(ReadFile(port, &c, 1, &got, 0));
        if (got)
        {
            if (c == '\n') { out[count] = 0; return count; }
            if (c != '\r' && count + 1 < capacity) out[count++] = c;
            continue;
        }
        if ((int)(GetTickCount() - end) > 0) return ~0u;
        Sleep(1);
    }
}
static void expect(const char *text)
{
    char answer[64];
    CHECK(line(answer, sizeof answer, 5000) == length(text) && same(answer, text, length(text)));
}
static U connection(void)
{
    char answer[64];
    U value = 0;
    send("WHO\r\n");
    CHECK(line(answer, sizeof answer, 5000) != ~0u && same(answer, "CONN ", 5));
    for (const char *p = answer + 5; *p >= '0' && *p <= '9'; ++p) value = value * 10 + (U)(*p - '0');
    return value;
}
static U open_port(const char *name) { return CreateFileA(name, 0xC0000000, 0, 0, 3, 0, 0); }

static void talk(void)
{
    static unsigned char block[20000];
    U n = 0, errors = 0, stat[3], lines = 0, mask = 0, first, started;
    char c;
    port = open_port("COM2");
    CHECK(port != ~0u && GetFileType(port) == 2);
    // One UART, one driver: a second open fails as under Windows.
    CHECK(open_port("COM2") == ~0u && GetLastError() == 5);
    CHECK(open_port("\\\\.\\COM2") == ~0u && GetLastError() == 5);
    CHECK(SetupComm(port, 4096, 4096));
    configure();
    CHECK(PurgeComm(port, 8 | 4));
    // Nothing was asked yet: the read returns at once with nothing.
    started = GetTickCount();
    CHECK(ReadFile(port, &c, 1, &n, 0) && n == 0 && GetTickCount() - started < 50);
    first = connection();
    send("PING 1\r\n");
    expect("PONG 1");
    // The lines of a connection that stands.
    CHECK(GetCommModemStatus(port, &lines) && lines == 0xb0);
    // ClearCommError counts what has arrived and is not read yet.
    send("PING 22\r\n");
    for (U i = 0; i < 5000; ++i)
    {
        CHECK(ClearCommError(port, &errors, stat));
        if (stat[1] == 9) break;
        Sleep(1);
    }
    CHECK(stat[1] == 9 && !errors);
    CHECK(ReadFile(port, block, 64, &n, 0) && n == 9 && same((char *)block, "PONG 22\r\n", 9));
    // 20000 bytes from the endpoint through the 4096 bytes of the receive
    // queue: none is lost and none is out of order.
    send("BLOB 20000\r\n");
    for (U got = 0, idle = 0; got < sizeof block;)
    {
        CHECK(ReadFile(port, block + got, sizeof block - got, &n, 0));
        got += n;
        if (n) idle = 0; else { CHECK(++idle < 5000); Sleep(1); }
    }
    for (U i = 0; i < sizeof block; ++i) CHECK(block[i] == (unsigned char)(i * 7 + 3));
    CHECK(ClearCommError(port, &errors, stat) && !errors && !stat[1]);
    // And 20000 bytes to it.
    for (U i = 0; i < sizeof block; ++i) block[i] = (unsigned char)(i * 13 + 5);
    send("SINK 20000\r\n");
    CHECK(WriteFile(port, block, sizeof block, &n, 0) && n == sizeof block);
    {
        U sum = 0;
        char want[32] = "SUM ", *p = want + 4, digits[12];
        int count = 0;
        for (U i = 0; i < sizeof block; ++i) sum += block[i];
        do digits[count++] = (char)('0' + sum % 10); while (sum /= 10);
        while (count) *p++ = digits[--count];
        *p = 0;
        expect(want);
    }
    // A total timeout: nothing comes, the read gives up after its time.
    {
        U timeouts[5] = {0, 0, 60, 0, 0};
        CHECK(SetCommTimeouts(port, timeouts));
        started = GetTickCount();
        CHECK(ReadFile(port, block, 4, &n, 0) && n == 0);
        // 60 ms asked; the guest's tick count and the scheduler's clock are
        // not the same clock, so a few ms less is seen (47 measured).
        CHECK(GetTickCount() - started >= 30 && GetTickCount() - started < 1500);
        // A byte that comes late ends the wait of a reader that stands still.
        timeouts[2] = 4000;
        CHECK(SetCommTimeouts(port, timeouts));
        send("LATE 100\r\n");
        started = GetTickCount();
        CHECK(ReadFile(port, &c, 1, &n, 0) && n == 1 && c == 'L');
        CHECK(GetTickCount() - started >= 50 && GetTickCount() - started < 3000);
    }
    // WaitCommEvent without OVERLAPPED waits for the byte.
    CHECK(SetCommMask(port, 1));
    send("LATE 60\r\n");
    CHECK(WaitCommEvent(port, &mask, 0) && mask == 1);
    configure();
    CHECK(ReadFile(port, &c, 1, &n, 0) && n == 1 && c == 'L');
    CHECK(SetCommMask(port, 0));
    // The output lines and a single character.
    CHECK(EscapeCommFunction(port, 6) && EscapeCommFunction(port, 5)); // CLRDTR, SETDTR
    CHECK(EscapeCommFunction(port, 4) && EscapeCommFunction(port, 3)); // CLRRTS, SETRTS
    CHECK(TransmitCommChar(port, 'P'));
    send("ING 3\r\n");
    expect("PONG 3");
    // The launcher's close: the last line must be out before the handle goes.
    send("LAST\r\n");
    CHECK(FlushFileBuffers(port));
    CHECK(CloseHandle(port));
    CHECK(!GetFileType(port));
    // The same port again is a new connection, and the endpoint has seen the
    // last line of the old one before it.
    port = open_port("COM2");
    CHECK(port != ~0u);
    configure();
    CHECK(connection() == first + 1);
    send("SEEN\r\n");
    expect("LAST 1");
    CHECK(CloseHandle(port));
}

static void absent(void)
{
    U n = 7, lines = 7, errors = 7, stat[3] = {7, 7, 7};
    char c;
    const U started = GetTickCount();
    port = open_port("COM1");
    CHECK(port != ~0u);
    configure();
    CHECK(PurgeComm(port, 8 | 4));
    for (U i = 0; i < 50; ++i)
    {
        CHECK(WriteFile(port, "AT\r\n", 4, &n, 0) && n == 4);
        CHECK(ReadFile(port, &c, 1, &n, 0) && n == 0);
        Sleep(10);
    }
    CHECK(GetCommModemStatus(port, &lines) && lines == 0);
    CHECK(ClearCommError(port, &errors, stat) && !errors && !stat[1] && !stat[2]);
    CHECK(FlushFileBuffers(port));
    CHECK(CloseHandle(port));
    // 50 rounds of 10 ms; looking for the endpoint must not slow them much.
    CHECK(GetTickCount() - started < 4000);
}

static void hold(U close)
{
    U timeouts[5] = {0, 0, 0, 0, 0}, n = 0;
    char c = 0;
    port = open_port("COM2");
    CHECK(port != ~0u);
    configure();
    send("HOLD\r\n");
    CHECK(SetCommTimeouts(port, timeouts)); // no timeouts: the read waits
    CHECK(ReadFile(port, &c, 1, &n, 0) && n == 1 && c == 'R');
    if (close) CHECK(CloseHandle(port));
}

static void after(void)
{
    port = open_port("COM2");
    CHECK(port != ~0u);
    configure();
    send("PING 9\r\n");
    expect("PONG 9");
    CHECK(CloseHandle(port));
}

U mainCRTStartup(void)
{
    const char *command = GetCommandLineA(), *word = command;
    // The last word of the command line names the part.
    for (const char *p = command; *p; ++p) if (*p == ' ' && p[1] && p[1] != ' ') word = p + 1;
    if (same(word, "talk", 4)) talk();
    else if (same(word, "absent", 6)) absent();
    else if (same(word, "hold", 4)) hold(1);
    else if (same(word, "leave", 5)) hold(0);
    else if (same(word, "refused", 7)) { CHECK(open_port("COM2") == ~0u && GetLastError() == 5); }
    else if (same(word, "after", 5)) after();
    else ExitProcess(1);
    ExitProcess(0);
    return 0;
}
