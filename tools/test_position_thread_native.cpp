// SPDX-License-Identifier: GPL-2.0-or-later
// Verify cross-thread SetWindowPos/ShowWindow callbacks execute on the
// window's owning thread. The guest checks each message and returns zero on
// success.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static void log(const char *text) { puts(text); }
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == size_t(n));
    fclose(f);
    return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    Files::System().MakeDirectory("C:\\WINDOWS\\TEMP");
    const unsigned windows_before = User::LiveWindows();
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "POSTHRD.EXE",
                      "POSTHRD.EXE"));
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(300);
    while (app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        app->Pump(8);
        if (app->Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app->Status() != X86::Application::Exited || app->ExitCode())
    {
        char text[256] = {};
        app->SentWaitText(0, text, sizeof text);
        fprintf(stderr, "POSITION THREAD status=%u code=%u error=%s\nthread 1 waits: %s\n",
                app->Status(), app->ExitCode(), app->Error(), text);
        assert(false);
    }
    const unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    delete app;
    assert(User::LiveWindows() == windows_before);
    printf("POSITION THREAD PASS calls=%u callbacks=%u\n", calls, callbacks);
    return 0;
}
