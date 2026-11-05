// SPDX-License-Identifier: GPL-2.0-or-later
// OLE32 and CRTDLL load at their preferred bases; the port's own pages
// (GDT, TEBs, process page) lie in its reservation 71000000h..72200000h.
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include <cassert>
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
    assert(!Files::System().Seed("C:\\PREFBASE.EXE", exe.data(), exe.size()));
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "PREFBASE.EXE", ""));
    for (unsigned n = 0; n < 40000 && app->Status() != X86::Application::Exited; ++n) {
        app->Pump(8);
        if (app->Status() == X86::Application::Failed)
            fprintf(stderr, "PREFERRED BASE failed: %s\n", app->Error());
        assert(app->Status() != X86::Application::Failed);
    }
    printf("PREFERRED BASE exit=%u\n", app->ExitCode());
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    delete app;
    printf("PREFERRED BASE PASS\n");
    return 0;
}
