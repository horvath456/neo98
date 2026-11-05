// SPDX-License-Identifier: GPL-2.0-or-later
// Test send cancellation on process/thread exit, window destruction and
// timeout, including sender exit. The parent fixture validates each result
// and returns zero.
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
    auto &files = Files::System();
    files.MakeDirectory("C:\\WINDOWS\\TEMP");
    files.MakeDirectory("C:\\SEND");
    assert(!files.Seed("C:\\SEND\\SENDLIFE.EXE", exe.data(), exe.size()));
    const unsigned windows_before = User::LiveWindows();
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "SENDLIFE.EXE",
                      "SENDLIFE.EXE"));
    // Eight child processes with millisecond waits can take several minutes
    // under ASan/UBSan on a busy machine.
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(900);
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
        fprintf(stderr,
                "SEND LIFETIME status=%u code=%u children=%u error=%s\nthread 1 waits: %s\n",
                app->Status(), app->ExitCode(), X86::Application::LiveChildren(), app->Error(),
                text);
        assert(false);
    }
    const unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    delete app;
    for (unsigned i = 0; i < 2000 && X86::Application::LiveChildren(); ++i)
        X86::Application::PumpChildren(4);
    assert(!X86::Application::LiveChildren() && !X86::Application::ChildRecords());
    assert(User::LiveWindows() == windows_before);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now() - started).count();
    printf("SEND LIFETIME PASS calls=%u callbacks=%u seconds=%lld\n", calls, callbacks,
           (long long)seconds);
    return 0;
}
