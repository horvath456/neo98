// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> data(size);
    assert(fread(data.data(), 1, data.size(), file) == data.size());
    fclose(file);
    return data;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto executable = load(argv[1]);
    const auto font = load(argv[2]);
    X86::Application app(nullptr);
    assert(app.Start(executable.data(), executable.size(), font.data(), font.size(),
                     "WINDOWDC.EXE", "WINDOWDC"));
    for (unsigned i = 0; i < 4000 && app.Status() != X86::Application::Exited &&
         app.Status() != X86::Application::Failed; ++i)
        app.Pump(32);
    const bool passed = app.Status() == X86::Application::Exited && !app.ExitCode();
    if (!passed)
        fprintf(stderr, "WINDOW-FROM-DC GUEST FAIL state=%u code=%u error=%s\n",
                app.Status(), app.ExitCode(), app.Error());
    printf("WINDOW-FROM-DC GUEST %s calls=%u callbacks=%u\n",
           passed ? "PASS" : "FAIL", app.ApiCalls(), app.Callbacks());
    assert(!User::LiveWindows());
    return passed ? 0 : 1;
}
