// SPDX-License-Identifier: GPL-2.0-or-later
// A remembered input thread must not retain input while it cannot retrieve
// it. With the foreground owner waiting on an event, route clicks and keys to
// the main thread pumping GetMessage.
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
    X86::Application app(log);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "INPUTTHREAD.EXE",
                     "INPUTTHREAD.EXE"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    bool sent = false;
    unsigned window = 0;
    while (app.Status() != X86::Application::Exited && app.Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        app.Pump(8);
        if (!sent)
        {
            unsigned node = 0;
            if (!files.Lookup("C:\\WINDOWS\\TEMP\\INPUT-THREAD.DAT", node))
            {
                const auto *file = files.Get(node);
                if (file && file->size == 4)
                {
                    memcpy(&window, file->data, 4);
                    Gdi::RECT bounds{};
                    assert(User::IsWindow(window) && User::GetWindowRect(window, &bounds));
                    // Target the main window's client center, not the blocked
                    // foreground window.
                    const int x = (bounds.left + bounds.right) / 2,
                              y = (bounds.top + bounds.bottom) / 2;
                    app.MouseButtons(x, y, 0);
                    app.MouseButtons(x, y, 1);
                    app.MouseButtons(x, y, 0);
                    app.RawKey('A', 'A', true, 0x1e, false);
                    app.RawKey('A', 0, false, 0x1e, false);
                    sent = true;
                }
            }
        }
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
    {
        fprintf(stderr, "INPUT THREAD status=%u code=%u sent=%u window=%u error=%s\n", app.Status(),
                app.ExitCode(), sent ? 1u : 0u, window, app.Error());
        assert(false);
    }
    assert(sent);
    printf("INPUT THREAD PASS calls=%u callbacks=%u\n", app.ApiCalls(), app.Callbacks());
    return 0;
}
