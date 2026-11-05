// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(length);
    assert(fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    fclose(file);
    return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    auto font = load(argv[4]);
    for (unsigned index = 1; index <= 3; ++index)
    {
        auto exe = load(argv[index]);
        auto *app = new X86::Application(nullptr);
        const bool started = app->Start(exe.data(), exe.size(), font.data(), font.size(), "CAPACITY.EXE");
        if (index == 3)
            assert(!started && app->Status() == X86::Application::Failed && !app->ApiCalls() &&
                   !strcmp(app->Error(), "Too many imports"));
        else
        {
            assert(started);
            for (unsigned step = 0; step < 1000 && app->Status() == X86::Application::Running; ++step)
                app->Pump();
            assert(app->Status() == X86::Application::Exited && !app->ExitCode() && app->Callbacks() == 1 &&
                   app->ApiCalls() == 3);
        }
        printf("IMPORT CAPACITY case=%u started=%u state=%u calls=%u callbacks=%u error=%s\n", index, started,
               app->Status(), app->ApiCalls(), app->Callbacks(), app->Error());
        delete app;
        assert(!User::LiveWindows() && !Gdi::LiveObjects());
    }
    puts("IMPORT CAPACITY PASS");
}
