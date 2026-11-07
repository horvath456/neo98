// SPDX-License-Identifier: GPL-2.0-or-later
// The capture-owning worker must retrieve its button release even while
// another thread owns the foreground window.
#include <cstring>
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
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
                    // The marker names the worker's captured window. A press
                    // and release are queued together, while thread 1 remains
                    // foreground and the worker filters for the release only.
                    const int x = (bounds.left + bounds.right) / 2,
                              y = (bounds.top + bounds.bottom) / 2;
                    app.MouseButtons(x, y, 0);
                    app.MouseButtons(x, y, 1);
                    app.MouseButtons(x, y, 0);


                    sent = true;
                }
            }
        }
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
    {
        fprintf(stderr, "CAPTURE THREAD status=%u code=%u sent=%u window=%u error=%s\n", app.Status(),
                app.ExitCode(), sent ? 1u : 0u, window, app.Error());
        assert(false);
    }
    assert(sent);
    printf("CAPTURE THREAD PASS calls=%u callbacks=%u\n", app.ApiCalls(), app.Callbacks());
    return 0;
}
