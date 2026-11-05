// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/user_system_state_fixture.c with the Win98 tree mounted and
// reports its exit code; zero means every check passed.
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdio>
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
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "STATE.EXE"));
    for (unsigned i = 0; i < 100000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++i)
    {
        app.Pump(64);
        // The fixture's Sleep(1) calls leave the guest idle; waiting keeps the
        // loop from running out before the guest's clock has moved.
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    printf("USER SYSTEM STATE status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    return app.Status() == X86::Application::Exited && !app.ExitCode() ? 0 : 1;
}
