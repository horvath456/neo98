// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == (unsigned)n);
    fclose(f);
    return bytes;
}
static void log(const char *text)
{
    printf("APP %s\n", text);
    fflush(stdout);
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    if (!app->Start(exe.data(), exe.size(), font.data(), font.size(), argv[3]))
    {
        printf("START FAILED %s\n", app->Error());
        delete app;
        return 1;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto state = app->Pump();
        if (state == X86::Application::Failed)
        {
            printf("FAILED %s\n", app->Error());
            delete app;
            return 1;
        }
        if (state == X86::Application::Exited)
        {
            unsigned code = app->ExitCode();
            printf("EXIT code=%u calls=%u callbacks=%u\n", code,app->ApiCalls(),app->Callbacks());
            delete app;
            assert(!User::LiveWindows()&&!Gdi::LiveObjects());
            return code ? 1 : 0;
        }
        if (app->WaitingForInput() && User::IsWindow(app->MainWindow()))
        {
            char title[256] = {};
            User::GetWindowText(app->MainWindow(), title, sizeof title);
            printf("IDLE title=%s windows=%u threads=%u calls=%u callbacks=%u\n", title, User::LiveWindows(),
                   app->LiveThreads(), app->ApiCalls(), app->Callbacks());
            FILE *out = fopen(argv[4], "wb");
            assert(out);
            fprintf(out, "P6\n800 600\n255\n");
            const auto *pixels = User::Pixels();
            for (unsigned i = 0; i < 800 * 600; ++i)
            {
                unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
                fwrite(rgb, 1, 3, out);
            }
            fclose(out);
            delete app;
            return 0;
        }
        if (state == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    puts("Probe reached its 30-second observation limit");
    delete app;
    return 2;
}
