// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/comm_host_fixture.c against an endpoint this runner serves: the
// POSIX serial host of tools/host_comm.h behind COM2, a port that does not
// answer behind COM1. The endpoint serves one client at a
// time, like the companion simulator of the reference system: a second connection is
// answered only when the first has ended.
#include "../src/x86_app.h"
#include "host_comm.h"
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using App = X86::Application;

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == size_t(n));
    fclose(f);
    return b;
}

// The far end of the cable.
struct Endpoint
{
    int listener = -1;
    unsigned port = 0;
    std::atomic<unsigned> connections{0}, ended{0}, last_lines{0}, holding{0};
    std::atomic<bool> release{false}, stop{false};
    std::thread thread;

    Endpoint()
    {
        listener = socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        assert(!bind(listener, (sockaddr *)&address, sizeof address) && !listen(listener, 4));
        socklen_t size = sizeof address;
        assert(!getsockname(listener, (sockaddr *)&address, &size));
        port = ntohs(address.sin_port);
        thread = std::thread([this] { Run(); });
    }
    ~Endpoint()
    {
        stop = true;
        // Wake the accept with a connection of our own.
        const int wake = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        connect(wake, (sockaddr *)&address, sizeof address);
        close(wake);
        thread.join();
        close(listener);
    }
    static void Send(int fd, const void *data, size_t count)
    {
        const char *p = static_cast<const char *>(data);
        while (count)
        {
            const ssize_t n = send(fd, p, count,
#ifdef MSG_NOSIGNAL
                                   MSG_NOSIGNAL
#else
                                   0
#endif
            );
            if (n <= 0) return;
            p += n;
            count -= size_t(n);
        }
    }
    void Run()
    {
        while (!stop)
        {
            const int fd = accept(listener, nullptr, nullptr);
            if (fd < 0 || stop) { if (fd >= 0) close(fd); continue; }
            int on = 1;
#ifdef SO_NOSIGPIPE
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#else
            (void)on;
#endif
            const unsigned number = ++connections;
            Serve(fd, number);
            close(fd);
            ++ended;
        }
    }
    void Serve(int fd, unsigned number)
    {
        std::string line;
        unsigned sink = 0, sum = 0;
        bool held = false;
        for (;;)
        {
            if (held && release.exchange(false)) { Send(fd, "R", 1); held = false; }
            pollfd wait{fd, POLLIN, 0};
            if (poll(&wait, 1, 5) <= 0) { if (stop) return; continue; }
            unsigned char chunk[4096];
            const ssize_t n = recv(fd, chunk, sizeof chunk, 0);
            if (n <= 0) return;
            for (ssize_t i = 0; i < n; ++i)
            {
                if (sink)
                {
                    sum += chunk[i];
                    if (!--sink) { char text[32]; Send(fd, text, size_t(snprintf(text, sizeof text, "SUM %u\r\n", sum))); }
                    continue;
                }
                if (chunk[i] != '\n') { if (chunk[i] != '\r') line += char(chunk[i]); continue; }
                char text[64];
                if (!line.compare(0, 5, "PING "))
                    Send(fd, text, size_t(snprintf(text, sizeof text, "PONG %s\r\n", line.c_str() + 5)));
                else if (line == "WHO") Send(fd, text, size_t(snprintf(text, sizeof text, "CONN %u\r\n", number)));
                else if (!line.compare(0, 5, "BLOB "))
                {
                    std::vector<unsigned char> blob(size_t(atoi(line.c_str() + 5)));
                    for (size_t k = 0; k < blob.size(); ++k) blob[k] = (unsigned char)(k * 7 + 3);
                    Send(fd, blob.data(), blob.size());
                }
                else if (!line.compare(0, 5, "SINK ")) sink = unsigned(atoi(line.c_str() + 5)), sum = 0;
                else if (!line.compare(0, 5, "LATE "))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(atoi(line.c_str() + 5)));
                    Send(fd, "L", 1);
                }
                else if (line == "LAST") ++last_lines;
                else if (line == "SEEN") Send(fd, text, size_t(snprintf(text, sizeof text, "LAST %u\r\n", last_lines.load())));
                else if (line == "HOLD") held = true, ++holding;
                line.clear();
            }
        }
    }
};

static const char *font_path, *exe_path;
static HostComm::Ports *host_ports;
// Whether COM2 still had its connection when the process had ended and its
// Application object was not yet deleted.
static bool connected_at_exit;
static App *start(const char *part)
{
    static std::vector<unsigned char> exe = load(exe_path), font = load(font_path);
    auto *app = new App(nullptr);
    std::string command = std::string("COMMHOST.EXE ") + part;
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "COMMHOST.EXE", command.c_str()));
    return app;
}
static bool done(App &app) { return app.Status() == App::Exited || app.Status() == App::Failed; }
static unsigned finish(App *app, const char *part, unsigned seconds = 60)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!done(*app) && std::chrono::steady_clock::now() < deadline)
        if (app->Pump(64) == App::Idle) std::this_thread::sleep_for(std::chrono::microseconds(200));
    const unsigned code = app->Status() == App::Exited ? app->ExitCode() : ~0u;
    printf("COMMHOST %s state=%u exit=%u calls=%u error=%s\n", part, app->Status(), app->ExitCode(), app->ApiCalls(), app->Error());
    fflush(stdout);
    for (unsigned i = 0; i < 4; ++i) app->Pump(1); // the pump of an ended process
    connected_at_exit = host_ports->Connected(1);
    delete app;
    return code;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    exe_path = argv[1];
    font_path = argv[2];
    Endpoint endpoint;
    // A port nobody listens on: bound, never listening, so a connection is refused.
    const int silent = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof address;
    assert(!bind(silent, (sockaddr *)&address, sizeof address) && !getsockname(silent, (sockaddr *)&address, &size));
    char com1[64], com2[64];
    snprintf(com1, sizeof com1, "tcp:127.0.0.1:%u", unsigned(ntohs(address.sin_port)));
    snprintf(com2, sizeof com2, "tcp:127.0.0.1:%u", endpoint.port);
    setenv("NEO98_COM1", com1, 1);
    setenv("NEO98_COM2", com2, 1);
    HostComm::Ports ports;
    ports.Install();
    host_ports = &ports;
    assert(CommHost::Attached(0) && CommHost::Attached(1) && !CommHost::Attached(2));

    // The endpoint does not answer: the port works and is silent.
    assert(finish(start("absent"), "absent") == 0);
    assert(!ports.Seen(0).connections && ports.Seen(0).lost == 200 && !ports.Seen(0).sent);
    assert(!endpoint.connections);

    // One program, the whole conversation, two connections one after the other.
    assert(finish(start("talk"), "talk") == 0);
    assert(endpoint.connections == 2 && ports.Seen(1).connections == 2 && !ports.Seen(1).lost);
    for (unsigned i = 0; i < 400 && endpoint.ended != 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(endpoint.ended == 2 && !ports.Connected(1));

    // Two processes of one machine: while one holds the port the other is
    // refused, and the port is free when the holder has closed it or ended.
    for (const char *part : {"hold", "leave"})
    {
        App *holder = start(part);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        const unsigned before = endpoint.holding;
        while (endpoint.holding == before && !done(*holder) && std::chrono::steady_clock::now() < deadline) holder->Pump(64);
        assert(endpoint.holding == before + 1 && !done(*holder));
        App *second = start("refused");
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!done(*second) && std::chrono::steady_clock::now() < limit) { second->Pump(64); holder->Pump(8); }
        assert(finish(second, "refused") == 0);
        assert(!done(*holder) && ports.Connected(1));
        endpoint.release = true;
        assert(finish(holder, part) == 0);
        // The process has ended; so has its connection, closed or not.
        assert(!connected_at_exit && !ports.Connected(1));
        assert(finish(start("after"), "after") == 0);
    }
    assert(endpoint.connections == 6);
    close(silent);
    puts("COMMHOST PASS");
    return 0;
}
