// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "host_volume.h"
#include "test_menu_input.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    auto size = ftell(f);
    rewind(f);
    std::vector<unsigned char> v(size);
    assert(fread(v.data(), 1, size, f) == (size_t)size);
    fclose(f);
    return v;
}
static void log(const char *message)
{
    printf("APP %s\n", message);
    fflush(stdout);
}
static void idle(X86::Application &app)
{
    for (unsigned i = 0; i < 100; ++i)
    {
        auto state = app.Pump();
        if (state == X86::Application::Idle)
            return;
        if (state == X86::Application::Failed)
        {
            fprintf(stderr, "FAILED %s\n", app.Error());
            abort();
        }
    }
    assert(false);
}
int main(int argc, char **argv)
{
    assert(argc >= 4);
    // The same workflow on either provider: --drive-c runs it on a writable
    // host directory.
    {
        auto drives = HostVolume::Scan(argc, argv);
        drives.ram = drives.drive_c == nullptr;
        assert(HostVolume::Mount(drives));
        if (!drives.ram) printf("FILE APP drive C %s\n", drives.drive_c);
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    Files::System().MakeDirectory("C:\\Documents"); // the file demo's folder (src/app_ui.cpp)
    assert(!Files::System().Seed("C:\\Documents\\NEO98.TXT", "Original file on native ARM.\r\nSecond line.",
                                 42));
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "NOTEPAD.EXE",
                      "NOTEPAD.EXE C:\\Documents\\NEO98.TXT"));
    idle(*app);
    char title[256], content[4096];
    User::GetWindowText(app->MainWindow(), title, sizeof title);
    unsigned edit = User::GetFocus();
    User::GetWindowText(edit, content, sizeof content);
    printf("FILE APP title=%s content=%s\n", title, content);
    assert(!strcmp(content, "Original file on native ARM.\nSecond line."));
    app->Key(35, 0, false, true);
    idle(*app);
    for (const char *p = " Saved by Win98."; *p; ++p)
    {
        app->Key(0, *p, false, false);
        idle(*app);
    }
    TestMenuInput::Command(*app,1,idle); // Mouse selection of original File > Save.
    idle(*app);
    Files::Session check(Files::System());
    unsigned handle, status, done;
    assert(!check.Open("C:\\Documents\\NEO98.TXT", 1, 3, 3, 0, handle, status));
    char bytes[256] = {};
    assert(!check.Read(handle, bytes, sizeof bytes, done));
    printf("FILE APP saved bytes=%u content=%s\n", done, bytes);
    assert(!strcmp(bytes, "Original file on native ARM.\r\nSecond line. Saved by Win98."));
    assert(!check.Close(handle));
    User::Render();
    FILE *out = fopen(argv[3], "wb");
    assert(out);
    fprintf(out, "P6\n800 600\n255\n");
    const auto *pixels = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i)
    {
        unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
    TestMenuInput::Command(*app,28,idle);
    for (unsigned i = 0; i < 100 && app->Status() != X86::Application::Exited; ++i)
        app->Pump();
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    const unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    delete app;
    assert(User::LiveWindows() == 0);
    assert(Gdi::LiveObjects() == 0);
    // A fresh original process reads the same RAM volume after the first exits.
    app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "NOTEPAD.EXE",
                      "NOTEPAD.EXE C:\\Documents\\NEO98.TXT"));
    idle(*app);
    User::GetWindowText(User::GetFocus(), content, sizeof content);
    assert(!strcmp(content, "Original file on native ARM.\nSecond line. Saved by Win98."));
    TestMenuInput::Command(*app,28,idle);
    for (unsigned i = 0; i < 100 && app->Status() != X86::Application::Exited; ++i)
        app->Pump();
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
    printf("NATIVE FILE APP PASS calls=%u callbacks=%u CRLF=exact process-reopen=PASS exit=0 windows=0 "
           "gdi_objects=0\n",
           calls, callbacks);
}
