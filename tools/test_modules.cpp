// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long n = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, file) == (unsigned)n);
    fclose(file);
    return bytes;
}
static void log(const char *text)
{
    printf("MODULE %s\n", text);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    char path[1024];
    const char *names[] = {"ALPHA.DLL", "BETA.DLL", "GAMMA.DLL",  "FAILED.DLL", "BAD.DLL",
                           "FIXED.DLL", "TLS.DLL",  "BROKEN.DLL", "CYCLEA.DLL", "CYCLEB.DLL"};
    for (const char *name : names)
    {
        snprintf(path, sizeof path, "%s/%s", argv[1], name);
        auto bytes = load(path);
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", name);
        assert(!Files::System().Seed(guest, bytes.data(), bytes.size()));
    }
    snprintf(path, sizeof path, "%s/MODULE.EXE", argv[1]);
    auto exe = load(path), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "MODULE.EXE"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        if (app->Pump() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    printf("MODULE CONTRACT state=%u code=%u calls=%u callbacks=%u error=%s\n", app->Status(),
           app->ExitCode(), app->ApiCalls(), app->Callbacks(), app->Error());
    fflush(stdout);
    assert(app->Status() == X86::Application::Exited && !app->ExitCode());
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
}
