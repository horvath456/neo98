// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "test_menu_input.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
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
    printf("APP %s\n", text);
    fflush(stdout);
}
static void idle(X86::Application &app)
{
    for (unsigned i = 0; i < 10000; ++i)
    {
        auto state = app.Pump();
        if (app.WaitingForInput())
            return;
        if (state == X86::Application::Failed)
        {
            fprintf(stderr, "FAILED %s\n", app.Error());
            abort();
        }
        if (state == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    abort();
}
static void capture(const char *path)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n800 600\n255\n");
    const auto *p = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i)
    {
        unsigned char rgb[] = {p[i * 4 + 2], p[i * 4 + 1], p[i * 4]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void click(X86::Application &app, int x, int y)
{
    app.Mouse(x, y, true, false);
    app.Mouse(x, y, false, true);
    idle(app);
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (std::chrono::steady_clock::now() < until)
    {
        idle(app);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    printf("CLICK %d %d\n", x, y);
}
int main(int argc, char **argv)
{
    assert(argc >= 4);
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "REGEDIT.EXE"));
    idle(*app);
    if (argc == 4)
    {
        // Original client-edge controls add two pixels inside the main client area.
        // Tree rows are 18 pixels. HKEY_LOCAL_MACHINE sits at 164 and now lists the
        // four imported children Config, Network, Software and System in that order,
        // so Software starts at 209 and its own child NEO98 at 227.
        click(*app, 98, 164);
        click(*app, 118, 218);
        click(*app, 183, 236);
        click(*app, 345, 148);
        TestMenuInput::Command(*app,912,idle,1); // Original Edit > Modify.
        idle(*app);
        unsigned dialog = User::FindWindow("#32770", "Edit String");
        assert(dialog && User::IsEdit(User::GetFocus()));
        app->Key('A', 1, false, true);
        idle(*app);
        const char *replacement = "ARM64 - edited by Win98 REGEDIT";
        for (const char *p = replacement; *p; ++p)
            app->Key(*p >= 'a' && *p <= 'z' ? *p - 32 : *p, *p, false, false);
        idle(*app);
        app->Key(13, 13, false, false);
        idle(*app);
        assert(!User::IsWindow(dialog));
        Registry::Session session(Registry::System());
        unsigned key = 0, node = 0;
        bool made = false;
        assert(!session.Open(0x80000002, "Software\\NEO98", false, key, made) && !session.Key(key, node));
        const Registry::Value *value = nullptr;
        assert(!session.store.Query(node, "Architecture", value) && value->type == 1 &&
               value->size == strlen(replacement) + 1 && !memcmp(value->data, replacement, value->size));
        assert(!session.Close(key));
    }
    else
        for (int i = 4; i + 1 < argc; i += 2)
            click(*app, atoi(argv[i]), atoi(argv[i + 1]));
    capture(argv[3]);
    unsigned frame = User::FrameSerial(), calls = app->ApiCalls();
    for (unsigned i = 0; i < 100; ++i)
        assert(app->Pump() == X86::Application::Idle);
    assert(frame == User::FrameSerial() && calls == app->ApiCalls());
    if (argc == 4)
    {
        Gdi::RECT r = {};
        assert(User::GetWindowRect(app->MainWindow(), &r));
        app->Mouse(r.right - 15, r.top + 14, true, false);
        app->Mouse(r.right - 15, r.top + 14, false, true);
        for (unsigned i = 0; i < 10000 && app->Status() != X86::Application::Exited; ++i)
        {
            auto state = app->Pump();
            if (state == X86::Application::Failed)
            {
                fprintf(stderr, "EXIT FAILED %s\n", app->Error());
                abort();
            }
            if (state == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    }
    printf("REGEDIT PASS calls=%u callbacks=%u exit=%u windows=%u\n", app->ApiCalls(), app->Callbacks(),
           app->ExitCode(), User::LiveWindows());
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
}
