// SPDX-License-Identifier: GPL-2.0-or-later
// the pointer over a program mode. tools/mode_pointer_fixture.c sets
// 640x480x8, covers the screen white and shows the arrow. Win98's display
// driver draws the pointer in every mode; the port drew none as soon as a
// program mode was active. The pointer is drawn in mode pixels and stretched
// with the picture (32 mode pixels are 40 host pixels at 640 to 800), a move
// gives the old place back, and ShowCursor(FALSE) takes the pointer away.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    auto n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == size_t(n));
    fclose(f);
    return b;
}

static constexpr unsigned Records = 0x30000000u;

// Host pixels in a box that are not white, and the extent they span.
struct Seen { unsigned pixels = 0; int left = 800, top = 600, right = -1, bottom = -1; };
static Seen Look(int x0, int y0, int x1, int y1)
{
    const unsigned char *frame = User::Pixels();
    assert(frame);
    Seen s;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const unsigned char *p = frame + (y * 800 + x) * 4;
            if (p[0] == 0xff && p[1] == 0xff && p[2] == 0xff) continue;
            ++s.pixels;
            if (x < s.left) s.left = x;
            if (y < s.top) s.top = y;
            if (x > s.right) s.right = x;
            if (y > s.bottom) s.bottom = y;
        }
    return s;
}

static void Wait(X86::Application *app, int stage)
{
    int record[4] = {};
    for (unsigned n = 0; n < 40000 && record[1] != stage; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
        app->Read(Records, record, sizeof record);
    }
    assert(record[1] == stage);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "MODEPTR.EXE", ""));
    Wait(app, 1);
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    assert(User::ScreenModeActive() && width == 640 && height == 480 && depth == 8);
    User::ShowPointer(true);
    // The arrow's hot spot is its tip: mode 320,240 is host 400,300.
    app->MouseButtons(320, 240, 0);
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    const Seen first = Look(360, 260, 480, 380);
    printf("MODE POINTER shown shape=%08x level=%d visible=%u pixels=%u box=%d,%d-%d,%d\n", User::PointerShape(), User::PointerCount(), User::PointerVisible() ? 1u : 0u,
           first.pixels, first.left, first.top, first.right, first.bottom);
    assert(User::PointerVisible());
    assert(first.pixels > 0);
    // The tip sits at the hot spot, and the shape is stretched like the
    // picture: the arrow is 12 by 19 mode pixels in Win98's OEM set, so more
    // than 12 host pixels wide and more than 19 high.
    assert(first.left == 400 && first.top == 300);
    assert(first.right - first.left + 1 > 12 && first.bottom - first.top + 1 > 19);
    // A move redraws the pointer at the new place and gives the old one back.
    app->MouseButtons(160, 120, 0);
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    const Seen old_place = Look(360, 260, 480, 380), moved = Look(160, 110, 300, 230);
    printf("MODE POINTER moved old=%u new=%u box=%d,%d\n", old_place.pixels, moved.pixels, moved.left, moved.top);
    assert(old_place.pixels == 0 && moved.pixels > 0 && moved.left == 200 && moved.top == 150);
    // A new frame from the program keeps the pointer on top.
    User::ScreenChanged();
    assert(Look(160, 110, 300, 230).pixels == moved.pixels);
    const int go = 1;
    assert(app->Store(Records + 8, &go, 4));
    Wait(app, 2);
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    const Seen hidden = Look(0, 0, 800, 600);
    printf("MODE POINTER hidden visible=%u pixels=%u\n", User::PointerVisible() ? 1u : 0u, hidden.pixels);
    assert(!User::PointerVisible() && hidden.pixels == 0);
    assert(app->Store(Records + 12, &go, 4));
    for (unsigned n = 0; n < 20000 && app->Status() != X86::Application::Exited; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
    }
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    delete app;
    printf("MODE POINTER PASS\n");
    return 0;
}
