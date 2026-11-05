// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
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
    puts(text);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "THREADS.EXE"));
    for (unsigned n = 0;
         n < 100000 && app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed;
         ++n)
        app->Pump();
    printf("THREAD FIXTURE state=%u code=%u threads=%u calls=%u error=%s\n", app->Status(), app->ExitCode(),
           app->ThreadsCreated(), app->ApiCalls(), app->Error());
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0 && app->ThreadsCreated() == 3);
    delete app;
    assert(!Gdi::LiveObjects() && !User::LiveWindows());
}
