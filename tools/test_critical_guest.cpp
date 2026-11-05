// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/critical_guest_fixture.c with the fast gates on ("fast", the
// guest code of the critical sections) or off ("slow", every call through the
// host) and prints what the guest counted and how the calls went: in the guest
// code, through the ordinary entry of EnterCriticalSection (the contended
// case) and of LeaveCriticalSection (a waiter to wake).
// tools/test_critical_guest.py checks the numbers.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

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

int main(int argc, char **argv)
{
    assert(argc == 4);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application::fast_gates = !strcmp(argv[3], "fast");
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "CRITICAL.EXE"));
    for (unsigned n = 0; n < 50000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
        app.Pump(1); // a thread switch at the end of every slice
    printf("CRITICAL status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        return 1;
    unsigned results[4] = {};
    assert(app.Read(0x30000000, results, sizeof results));
    unsigned enter = 0, leave = 0;
    for (unsigned i = 0; i < app.import_count; ++i)
    {
        if (!strcmp(app.imports[i].name, "EnterCriticalSection")) enter += app.imports[i].calls;
        if (!strcmp(app.imports[i].name, "LeaveCriticalSection")) leave += app.imports[i].calls;
    }
    printf("CRITICAL mode=%s counter=%u collisions=%u long_rounds=%u guest=%llu fast=%llu slow=%llu enter_host=%u leave_host=%u\n",
           argv[3], results[0], results[1], results[3], app.guest_gate_calls, app.fast_gate_calls, app.slow_gate_calls,
           enter, leave);
    return 0;
}
