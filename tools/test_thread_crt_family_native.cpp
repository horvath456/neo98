// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include <cassert>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = std::fopen(path, "rb"); assert(f);
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); assert(n > 0);
    std::rewind(f); std::vector<unsigned char> bytes(n);
    assert(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size()); std::fclose(f); return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    X86::Application app(nullptr);
    assert(FixtureModules::KeepNative(app));
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "THREAD-FAMILY.EXE"));
    for (unsigned i = 0; i < 20000 && app.Status() != X86::Application::Failed &&
         app.Status() != X86::Application::Exited; ++i) app.Pump(32);
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        std::fprintf(stderr, "Thread family failure: state=%u exit=%u %s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    std::printf("THREAD CRT FAMILY NATIVE PASS: calls=%u callbacks=%u threads=%u\n",
                app.ApiCalls(), app.Callbacks(), app.ThreadsCreated());
}
