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
static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == (unsigned)n);
    fclose(f);
    return b;
}
static void log(const char *p)
{
    printf("APP %s\n", p);
    fflush(stdout);
}
static void idle(X86::Application &app)
{
    for (unsigned n = 0; n < 10000; ++n)
    {
        auto s = app.Pump();
        if (app.WaitingForInput())
            return;
        if (s == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (s == X86::Application::Failed)
        {
            fprintf(stderr, "FAILED %s\n", app.Error());
            abort();
        }
    }
    fprintf(stderr, "Idle budget exceeded\n");
    abort();
}
static void click(X86::Application &app, unsigned id)
{
    unsigned h = User::GetDlgItem(app.MainWindow(), id);
    assert(h);
    Gdi::RECT r = {};
    assert(User::GetWindowRect(h, &r));
    int x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    app.Mouse(x, y, true, false);
    app.Mouse(x, y, false, true);
    idle(app);
    char text[256];
    User::GetWindowText(User::GetDlgItem(app.MainWindow(), 403), text, sizeof text);
    printf("CLICK %u display=%s x=%d y=%d\n", id, text, x, y);
    fflush(stdout);
}
static void display(X86::Application &app, const char *expected)
{
    char text[256];
    User::GetWindowText(User::GetDlgItem(app.MainWindow(), 403), text, sizeof text);
    printf("DISPLAY %s expected=%s threads=%u created=%u\n", text, expected, app.LiveThreads(),
           app.ThreadsCreated());
    fflush(stdout);
    assert(!strcmp(text, expected));
}
static void number(X86::Application &app, const char *text)
{
    for (; *text; ++text)
        click(app, *text == '.' ? 85 : 124 + *text - '0');
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "CALC.EXE"));
    idle(*app);
    assert(User::LiveWindows() == 32);
    display(*app, "0.");
    Gdi::RECT first_backspace = {};
    assert(User::GetWindowRect(User::GetDlgItem(app->MainWindow(), 83), &first_backspace));
    std::vector<unsigned char> first(User::Pixels(), User::Pixels() + 800 * 600 * 4);
    app->Key('9', '9', false, false);
    app->Key('8', '8', false, false);
    idle(*app);
    display(*app, "98.");
    click(*app, 81);
    number(*app, "7");
    click(*app, 92);
    number(*app, "8");
    click(*app, 112);
    display(*app, "15.");
    assert(app->ThreadsCreated() > 1);
    click(*app, 81);
    number(*app, "12.5");
    click(*app, 91);
    number(*app, "8");
    click(*app, 112);
    display(*app, "100.");
    click(*app, 81);
    number(*app, "144");
    click(*app, 103);
    display(*app, "12.");
    click(*app, 115);
    click(*app, 81);
    click(*app, 114);
    display(*app, "12.");
    click(*app, 113);
    // Exercise original resource replacement and destruction beyond the dialog table size.
    for (unsigned i = 0; i < 20; ++i)
    {
        TestMenuInput::Command(*app,303,idle);
        idle(*app);
        assert(User::LiveWindows() == 76);
        TestMenuInput::Command(*app,304,idle);
        idle(*app);
        assert(User::LiveWindows() == 32);
    }
    click(*app, 81);
    number(*app, "98");
    click(*app, 91);
    number(*app, "98");
    click(*app, 112);
    display(*app, "9604.");
    // The unchanged Backspace glyphs must survive reallocations and dialog replacement.
    Gdi::RECT backspace = {};
    assert(User::GetWindowRect(User::GetDlgItem(app->MainWindow(), 83), &backspace));
    assert(first_backspace.right - first_backspace.left == backspace.right - backspace.left &&
           first_backspace.bottom - first_backspace.top == backspace.bottom - backspace.top);
    for (int y = 5; y < 24; ++y)
        for (int x = 3; x < 60; ++x)
            for (unsigned c = 0; c < 3; ++c)
                assert(first[((first_backspace.top + y) * 800 + first_backspace.left + x) * 4 + c] ==
                       User::Pixels()[((backspace.top + y) * 800 + backspace.left + x) * 4 + c]);
    unsigned frame = User::FrameSerial(), calls = app->ApiCalls();
    for (unsigned i = 0; i < 100; ++i)
    {
        assert(app->Pump() == X86::Application::Idle);
        assert(!User::Render());
    }
    assert(frame == User::FrameSerial() && calls == app->ApiCalls());
    FILE *out = fopen(argv[3], "wb");
    assert(out);
    fprintf(out, "P6\n800 600\n255\n");
    const auto *px = User::Pixels();
    for (unsigned n = 0; n < 800 * 600; ++n)
    {
        unsigned char rgb[] = {px[n * 4 + 2], px[n * 4 + 1], px[n * 4]};
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
    Gdi::RECT frame_rect={};assert(User::GetWindowRect(app->MainWindow(),&frame_rect));
    app->Mouse(frame_rect.right-15,frame_rect.top+15,true,false);
    app->Mouse(frame_rect.right-15,frame_rect.top+15,false,true);
    for(unsigned i=0;i<10000&&app->Status()!=X86::Application::Exited;++i){
        auto status=app->Pump();if(status==X86::Application::Failed){fprintf(stderr,"Exit failed: %s\n",app->Error());abort();}
        if(status==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(app->Status()==X86::Application::Exited&&app->ExitCode()==0);
    printf("CALCULATOR PASS calls=%u callbacks=%u threads_created=%u mode_changes=40\n", app->ApiCalls(),
           app->Callbacks(), app->ThreadsCreated());
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
}
