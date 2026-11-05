// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "system_tree_host.h"
#include "test_menu_input.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cassert>
#include <cstring>
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
    assert(argc == 4);
    auto exe = load(argv[1]), font = load(argv[2]);
    // NOTEPAD.EXE imports SHELL32 and COMDLG32, and both run as original x86
    // code since PLAN.md step 7, so the run needs the imported Win98 tree.
    //
    assert(SystemTreeHost::Mount());
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size()));
    idle(*app);
    char title[256];
    User::GetWindowText(app->MainWindow(), title, sizeof title);
    assert(!strcmp(title, "Untitled - Notepad"));
    assert(User::LiveWindows() == 2);
    const char *text = "Original Win98 Notepad on ARM.";
    for (const char *p = text; *p; ++p)
    {
        app->Key(0, *p, false, false);
        idle(*app);
    }
    unsigned edit = User::GetFocus();
    char content[4096];
    User::GetWindowText(edit, content, sizeof content);
    assert(!strcmp(content, text));
    app->Mouse(122, 92, true, false);
    app->Mouse(122, 92, false, true);
    idle(*app); // Original WM_INITMENU also queries the empty WIN.INI for printer settings.
    app->Mouse(780, 540, true, false);
    app->Mouse(780, 540, false, true);
    idle(*app);
    for (unsigned i = 0; i < 10; ++i)
    {
        unsigned old = User::GetFocus();
        TestMenuInput::Command(*app,27,idle);
        idle(*app);
        assert(!User::IsWindow(old));
        assert(User::LiveWindows() == 2);
        User::GetWindowText(User::GetFocus(), content, sizeof content);
        assert(!strcmp(content, text));
    }
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
    const unsigned before = app->ApiCalls(), frame = User::FrameSerial();
    for (unsigned i = 0; i < 100; ++i)
        assert(app->Pump() == X86::Application::Idle);
    assert(app->ApiCalls() == before && !User::Render() && User::FrameSerial() == frame);
    // The original unsaved-document path enters MessageBox from inside its WndProc.
    // Pump must return while that x86/C++ call chain waits for real dialog input.
    unsigned edit_focus=User::GetFocus();
    TestMenuInput::Command(*app,28,idle);
    idle(*app);
    unsigned button=User::GetFocus(),dialog=User::Parent(button);
    assert(dialog && dialog!=app->MainWindow() && User::IsButton(button));
    assert(!User::IsEnabled(app->MainWindow()));
    app->Key(27,0,false,false);idle(*app);
    assert(User::IsEnabled(app->MainWindow()) && User::IsWindow(edit_focus));
    User::GetWindowText(edit_focus,content,sizeof content);assert(!strcmp(content,text));
    assert(User::LiveWindows()==2);
    app->Send(edit_focus,0xb9,0);
    // End of the modal test. The following exit uses an explicitly unmodified fixture.
    app->Send(User::GetFocus(), 0xb9, 0); // Test fixture explicitly marks this throwaway document unmodified.
    TestMenuInput::Command(*app,28,idle);
    for (unsigned i = 0; i < 100 && app->Status() != X86::Application::Exited; ++i)
    {
        app->Pump();
        if (app->Status() == X86::Application::Failed)
        {
            fprintf(stderr, "Exit failed: %s\n", app->Error());
            abort();
        }
    }
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0 && User::LiveWindows() == 0);
    const unsigned calls = app->ApiCalls(), callbacks = app->Callbacks();
    delete app;
    assert(User::LiveWindows() == 0);
    if (Gdi::LiveObjects())
    {
        fprintf(stderr, "GDI teardown retains %u objects\n", Gdi::LiveObjects());
        for (unsigned word = 1; word <= Gdi::ObjectCapacity(); ++word)
            if (const unsigned handle = Gdi::FullHandle(word | 0x8000u)) {
                int width = 0, height = 0;
                Gdi::BitmapDimensions(handle, width, height);
                fprintf(stderr, "GDI retained handle=%x type=%u owner=%u bitmap=%dx%d\n", handle,
                        Gdi::GetObjectType(handle), Gdi::GetObjectOwner(handle), width, height);
            }
    }
    assert(Gdi::LiveObjects() == 0);
    printf("NATIVE APP PASS title=%s calls=%u callbacks=%u edit=%u text=%s exit=0 windows=0 gdi_objects=0\n",
           title, calls, callbacks, edit, content);
}
