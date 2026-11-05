/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_wininet_guest.py: the original WININET.DLL of the
   tree fetches a page from a local HTTP server. The command line carries the
   port on 127.0.0.1. Every answer goes out as "NET <name>=<hex>". */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetLastError(void);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U ReadFile(U, void *, U, U *, void *);
API U CloseHandle(U);
API U URLDownloadToFileA(void *, const char *, const char *, U, void *);

API U InternetGetConnectedState(U *, U);
API U InternetOpenA(const char *, U, const char *, const char *, U);
API U InternetOpenUrlA(U, const char *, const char *, U, U, U);
API U InternetReadFile(U, void *, U, U *);
API U HttpQueryInfoA(U, U, void *, U *, U *);
API U InternetCloseHandle(U);
API U InternetConnectA(U, const char *, unsigned short, const char *, const char *, U, U, U);
API U HttpOpenRequestA(U, const char *, const char *, const char *, const char *, const char **, U, U);
API U HttpSendRequestA(U, const char *, U, void *, U);

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

static U port;
static char url[128];
static const char *given;   /* a whole URL on the command line, for runs by hand */
static void Url(const char *path)
{
    if (given)
    {
        U i = 0;
        while (given[i] && i < sizeof url - 1) { url[i] = given[i]; ++i; }
        url[i] = 0;
        return;
    }
    static const char head[] = "http://127.0.0.1:";
    char digits[8];
    U at = 0, i = 0, n = 0, p = port;
    while (head[i]) url[at++] = head[i++];
    do { digits[n++] = (char)('0' + p % 10); p /= 10; } while (p);
    while (n) url[at++] = digits[--n];
    for (i = 0; path[i]; ++i) url[at++] = path[i];
    url[at] = 0;
}

void mainCRTStartup(void)
{
    static const char expected[] = "NEO98 WININET page\r\n";
    static char body[4096];
    const char *p = GetCommandLineA();
    U flags = 0, session, request, got = 0, read = 0, status = 0, length = 4, index = 0, ok;
    while (*p && *p != ' ') ++p;
    while (*p == ' ') ++p;
    if (p[0] == 'h') given = p;
    while (*p >= '0' && *p <= '9') port = port * 10 + (U)(*p++ - '0');

    ok = InternetGetConnectedState(&flags, 0);
    number("connected", ok);
    number("connected_flags", flags);

    session = InternetOpenA("NEO98 WININET test", 0 /* PRECONFIG */, 0, 0, 0);
    number("open_ok", session != 0);
    if (!session) { number("open_error", GetLastError()); ExitProcess(1); }
    Url("/page.txt");
    request = InternetOpenUrlA(session, url, 0, 0, 0x80000000u | 0x04000000u /* RELOAD, NO_CACHE_WRITE */, 0);
    number("url_ok", request != 0);
    if (!request) { number("url_error", GetLastError()); ExitProcess(2); }
    number("status_query", HttpQueryInfoA(request, 19 | 0x20000000 /* STATUS_CODE, NUMBER */, &status, &length, &index));
    number("status", status);
    for (;;)
    {
        if (!InternetReadFile(request, body + got, sizeof body - got, &read)) { number("read_error", GetLastError()); break; }
        if (!read) break;
        got += read;
    }
    number("bytes", got);
    number("body", got == sizeof expected - 1 && same(body, expected, got));
    InternetCloseHandle(request);

    /* A page that is not there: the server's 404 comes through. */
    Url("/missing.txt");
    request = InternetOpenUrlA(session, url, 0, 0, 0x80000000u | 0x04000000u, 0);
    number("missing_ok", request != 0);
    status = 0; length = 4; index = 0;
    if (request) HttpQueryInfoA(request, 19 | 0x20000000, &status, &length, &index);
    number("missing_status", status);
    if (request) InternetCloseHandle(request);
    InternetCloseHandle(session);

    /* URLMON on top of WININET: the page into a file of drive C:. */
    Url("/page.txt");
    number("urlmon", URLDownloadToFileA(0, url, "C:\\PAGE.TXT", 0, 0));
    {
        U file = CreateFileA("C:\\PAGE.TXT", 0x80000000u, 1, 0, 3, 0x80, 0), n = 0;
        static char copy[256];
        if (file != ~0u)
        {
            ReadFile(file, copy, sizeof copy, &n, 0);
            CloseHandle(file);
        }
        number("urlmon_file", n == sizeof expected - 1 && same(copy, expected, n));
    }
    number("done", 1);
    ExitProcess(0);
}
