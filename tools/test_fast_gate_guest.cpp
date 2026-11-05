// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/fast_gate_fixture.c once, with the fast API gates on or off, and
// writes what the guest could observe of every call: return value, stack
// pointer, the four registers a stdcall callee must preserve, and the last
// error. tools/test_fast_gate_guest.py runs this twice, in two processes so
// that process and thread numbers start the same way, and compares.
//
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <time.h>

static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    auto n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == size_t(n));
    fclose(f);
    return b;
}

static constexpr unsigned Records = 0x30000000u, Fields = 8u;

int main(int argc, char **argv)
{
    assert(argc == 4);
    const bool fast = !strcmp(argv[3], "fast");
    // The scheduler reads the paused machine clock on each call. A pause
    // longer than a time slice must not consume that slice after resume.
    using App = X86::Application;
    const auto started = App::Now();
    App::PauseMachine(true);
    const auto paused = App::Now();
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    assert(App::Now() == paused && paused >= started);
    App::SetNow(123456);
    assert(App::Now() == 123456);
    App::PauseMachine(false);
    const auto host = App::HostNow();
    const auto resumed = App::Now();
    assert(resumed >= 123456);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(App::Now() - resumed >= 4000);
    assert(App::HostNow() >= host);
    App::SetNow(started);
    App::PauseMachine(false);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application::fast_gates = fast;
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "FASTGATE.EXE"));
    if (fast) {
        unsigned direct = 0, traced = 0;
        assert(app.NativeProcedure("KERNEL32.DLL", "TlsGetValue", 0, &direct, false));
        const unsigned probe[] = {direct, 1};
        assert(!app.PointerProbeApi("IsBadCodePtr", probe));
        assert(!app.PointerProbeApi("IsBadReadPtr", probe));
        assert(app.PointerProbeApi("IsBadWritePtr", probe));
        VirtualMemory::Info info{};
        assert(app.virtual_memory.Query(direct, info) && info.state == 0x1000 && info.protection == 2);
        assert(direct >= App::TrivialCodeBase && direct < App::TrivialCodeBase + 4096);
        char description[128]{};
        app.DescribeCodeAddress(direct, description, sizeof description);
        assert(strstr(description, "KERNEL32.DLL!TlsGetValue"));
        app.trace_calls = true;
        assert(app.NativeProcedure("KERNEL32.DLL", "TlsGetValue", 0, &traced, false));
        assert(traced >= App::GateBase && traced < App::GateBase + App::GateRegionBytes);
        app.trace_calls = false;
        unsigned gate = 0;
        assert(app.NativeProcedure("KERNEL32.DLL", "GetCPInfo", 0, &gate, false));
        auto &item = app.imports[(gate - App::GateBase) / 16];
        app.BindRoute(item);
        app.ArmFastGate(item, gate);
        const bool initialized = app.system_locale.initialized;
        app.system_locale.initialized = false;
        const auto calls = app.ApiCalls();
        assert(!app.FastGate(gate) && app.ApiCalls() == calls);
        app.system_locale.initialized = initialized;
        assert(app.NativeProcedure("KERNEL32.DLL", "MultiByteToWideChar", 0, &gate, false));
        auto &conversion = app.imports[(gate - App::GateBase) / 16];
        app.BindRoute(conversion);
        app.ArmFastGate(conversion, gate);
        const auto stack = app.Register(X86::Reg::Esp);
        unsigned saved[7]{};
        assert(app.Read(stack, saved, sizeof saved));
        // Reach the argument fallback independently of the startup guard.
        app.system_locale.initialized = true;
        unsigned unsupported[7] = {0, 65000, 0, 0, 0, 0, 0};
        assert(app.Store(stack, unsupported, sizeof unsupported));
        assert(!app.FastGate(gate) && app.ApiCalls() == calls);
        assert(app.Register(X86::Reg::Esp) == stack);
        unsupported[1] = 1252;
        unsupported[2] = 2;
        assert(app.Store(stack, unsupported, sizeof unsupported));
        assert(!app.FastGate(gate) && app.ApiCalls() == calls);
        assert(app.Register(X86::Reg::Esp) == stack);
        assert(app.Store(stack, saved, sizeof saved));
        app.system_locale.initialized = initialized;
        app.trace_calls = true;
        assert(!app.FastGateReturn(item));
        app.trace_calls = false;
    }
    for (unsigned n = 0; n < 200000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
        app.Pump(32);
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "FASTGATE status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    unsigned count = 0;
    assert(app.Read(Records, &count, 4) && count && count < 4096);
    std::vector<unsigned> fields(count * Fields);
    assert(app.Read(Records + 4, fields.data(), count * Fields * 4));

    printf("FASTGATE mode=%s records=%u fast=%llu slow=%llu withdrawn=%llu guest=%llu\n", argv[3], count,
           app.fast_gate_calls, app.slow_gate_calls, app.fast_gate_withdrawn, app.guest_gate_calls);
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned *f = fields.data() + i * Fields;
        printf("RECORD %u %08x %08x %08x %08x %08x %08x %08x\n", f[0], f[1], f[2], f[3], f[4], f[5],
               f[6], f[7]);
    }
    return 0;
}
