// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
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
    assert(argc == 3 || argc == 4);
    // The CryptoAPI part loads RSA.DLL and reads ADVAPI32.DLL of the
    // installation, so the imported Win98 tree is mounted under drive C:.
    if (argc == 4) assert(SystemTreeHost::Mount(argv[3]));
    auto exe = load(argv[1]), font = load(argv[2]);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "ADVREG.EXE"));
    for (unsigned n = 0; n < 20000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "ADVREG status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    // Every registration of the run is gone: the fired ones dropped themselves,
    // the process destructor takes the rest.
    printf("ADVAPI32 registry guest PASS calls=%u\n", app.ApiCalls());
    return 0;
}
