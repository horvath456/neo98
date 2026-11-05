// SPDX-License-Identifier: GPL-2.0-or-later
// Runner of tools/thread_capacity_fixture.c.
#include "../src/x86_app.h"
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
    if (!strncmp(text, "CreateThread", 12))
        puts(text);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "CAPACITY.EXE"));
    const time_t until = time(nullptr) + 300;
    while (time(nullptr) < until && app->Status() != X86::Application::Exited &&
           app->Status() != X86::Application::Failed)
        app->Pump();
    printf("THREAD CAPACITY state=%u code=%u threads=%u error=%s\n", app->Status(), app->ExitCode(),
           app->ThreadsCreated(), app->Error());
    const bool okay = app->Status() == X86::Application::Exited && app->ExitCode() == 0;
    delete app;
    return okay ? 0 : 1;
}
