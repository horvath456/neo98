// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == (unsigned)n);
    fclose(f);
    return b;
}
static void log(const char *s)
{
    puts(s);
    fflush(stdout);
}
static void idle(X86::Application &app)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto settled = std::chrono::steady_clock::now();
    unsigned calls = app.ApiCalls(), callbacks = app.Callbacks();
    while (std::chrono::steady_clock::now() < end)
    {
        auto state = app.Pump();
        if (state == X86::Application::Failed || state == X86::Application::Exited)
            return;
        // A pump can yield while GetMessage is still delivering queued input.
        // Wait for its callbacks and calls to settle before inspecting state.
        const auto now = std::chrono::steady_clock::now();
        if (!app.WaitingForInput() || calls != app.ApiCalls() || callbacks != app.Callbacks())
            settled = now;
        calls = app.ApiCalls(); callbacks = app.Callbacks();
        if (app.WaitingForInput() && now - settled >= std::chrono::milliseconds(20))
            return;
        if (state == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(!"Taskman did not settle");
}
static void capture(const char *path)
{
    const auto *pixels = User::Pixels();
    FILE *out = fopen(path, "wb");
    assert(out);
    fprintf(out, "P6\n800 600\n255\n");
    for (unsigned n = 0; n < 800 * 600; ++n)
    {
        unsigned char rgb[] = {pixels[n * 4 + 2], pixels[n * 4 + 1], pixels[n * 4]};
        assert(fwrite(rgb, 1, 3, out) == 3);
    }
    fclose(out);
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    assert(SystemTreeHost::Mount());
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "TASKMAN.EXE"));
    idle(*app);
    unsigned list = 0;
    for (auto h = User::GetTopWindow(app->MainWindow()); h; h = User::GetWindow(h, 2))
        if (User::CommonControl(h) == 4)
            list = h;
    assert(list);
    auto count = [&](unsigned expected)
    {
        assert(app->WaitingForInput());
        unsigned actual = 0, selection = 0;
        assert(app->InspectListBox(list, actual, selection));
        printf("TASKMAN COUNT actual=%u expected=%u state=%u\n", actual, expected, app->Status());
        fflush(stdout);
        assert(app->Status() != X86::Application::Failed && actual == expected);
    };
    count(0);
    assert(User::RegisterClass("NEO98TaskmanCandidate", 0, 0, 16));
    auto create = [&](const char *title, int y)
    {
        auto h = User::CreateWindow("NEO98TaskmanCandidate", title, 0x10cf0000, 450, y, 280, 140, 0, 0, 0);
        assert(h);
        User::CompleteCreation(h);
        idle(*app);
        return h;
    };
    const auto first = create("Native ARM window one", 50);
    count(1);
    const auto second = create("Native ARM window two", 220);
    count(2);
    assert(User::SetWindowText(first, "Renamed ARM window"));
    idle(*app);
    count(2);
    capture(argv[3]);
    User::ShowWindow(second, 0);
    idle(*app);
    count(1);
    User::ShowWindow(second, 5);
    idle(*app);
    count(2);
    assert(User::DestroyWindow(first));
    idle(*app);
    count(1);
    assert(User::DestroyWindow(second));
    idle(*app);
    count(0);
    const auto candidate = create("Restore this ARM window", 50);
    count(1);
    User::ShowWindow(candidate, 6);
    assert(User::Style(candidate) & 0x20000000);
    app->Mouse(80, 80, true, false);
    app->Mouse(80, 80, false, true);
    idle(*app);
    // TASKMAN's subclass posts command 1A8h for WM_CHAR Return.
    // A mouse click only selects; its WM_COMMAND handler ignores LBN_DBLCLK.
    app->RawKey(0x0d, 0, true, 0x1c);
    app->RawKey(0x0d, 0, false, 0x1c);
    idle(*app);
    assert(app->WaitingForInput() && User::GetForegroundWindow() == candidate);
    assert(!(User::Style(candidate) & 0x20000000) && (User::Style(app->MainWindow()) & 0x20000000));
    printf("TASKMAN SWITCH PASS target=%u foreground=%u restored=1 taskman_minimized=1\n", candidate,
           User::GetForegroundWindow());
    char switched[1024];
    snprintf(switched, sizeof switched, "%s.switched.ppm", argv[3]);
    capture(switched);
    // Restore Taskman by delivering SC_RESTORE on its application fiber.
    assert(User::PostMessage(app->MainWindow(), 0x112, 0xf120, 0));
    idle(*app);
    assert(app->WaitingForInput() && !(User::Style(app->MainWindow()) & 0x20000000));
    count(1);
    assert(User::DestroyWindow(candidate));
    idle(*app);
    count(0);
    app->Mouse(376, 24, true, false);
    app->Mouse(376, 24, false, true);
    idle(*app);
    assert(app->Status() == X86::Application::Exited && !app->ExitCode());
    printf("TASKMAN NATIVE PASS counts=0,1,2,2,1,2,1,0 calls=%u callbacks=%u exit=0\n", app->ApiCalls(),
           app->Callbacks());
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
}
