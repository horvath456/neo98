// SPDX-License-Identifier: GPL-2.0-or-later
// Runner of tools/view_resource_fixture.c.
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>
static std::vector<unsigned char> load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == (unsigned)n);
    fclose(f);
    return b;
}
static void log(const char *text)
{
    if (!strncmp(text, "MISSING", 7))
        puts(text);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "VIEWRES.EXE"));
    const time_t until = time(nullptr) + 120;
    while (time(nullptr) < until && app->Status() != X86::Application::Exited &&
           app->Status() != X86::Application::Failed)
        app->Pump();
    printf("VIEW RESOURCE state=%u code=%u error=%s\n", app->Status(), app->ExitCode(), app->Error());
    const bool okay = app->Status() == X86::Application::Exited && app->ExitCode() == 0;
    delete app;
    return okay ? 0 : 1;
}
