// SPDX-License-Identifier: GPL-2.0-or-later
// Dev-C++. Runs tools/paint_limits_fixture.c and checks three limits:
// a thousand compatible bitmaps at once (the bitmap store had about two hundred
// and the GDI32 thunk answered the rest with error 87), a BUTTON painted while
// another window's WM_PAINT runs a modal loop (the port painted no control
// while any paint was open), and BeginPaint without a DC still validating the
// window (USER takes the update region off before asks for the
// DC; the port left it and sent WM_PAINT forever), and UpdateWindow painting
// the children of a window at once.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
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

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "LIMITS.EXE"));
    int record[11] = {};
    auto read = [&] { app->Read(Records, record, sizeof record); };
    for (unsigned n = 0; n < 40000 && record[0] != 1; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
        read();
    }
    printf("PAINT LIMITS bitmaps=%d first_error=%d nested=%d button=%d\n", record[1], record[7], record[0],
           record[2]);
    assert(record[1] == 1000);
    assert(record[0] == 1 && record[2]);
    bool has = false, drawn = false;
    for (unsigned n = 0; n < 4000 && !drawn; ++n) {
        app->Pump(8);
        User::CanvasInfo(unsigned(record[2]), has, drawn);
    }
    printf("PAINT LIMITS button canvas=%u drawn=%u during the open paint\n", has ? 1u : 0u, drawn ? 1u : 0u);
    assert(has && drawn);
    const int go = 1;
    assert(app->Store(Records + 24, &go, 4));
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (app->Status() != X86::Application::Exited && std::chrono::steady_clock::now() < end) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
    }
    read();
    printf("PAINT LIMITS big window paints=%d dc=%x update_left=%d done=%d state=%u exit=%u\n", record[3],
           unsigned(record[4]), record[5], record[8], unsigned(app->Status()), app->ExitCode());
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0 && record[8] == 1);
    printf("PAINT LIMITS child paints after UpdateWindow=%d\n", record[10]);
    assert(record[10] == 1);
    // One WM_PAINT, whatever BeginPaint answered, and no region left behind.
    assert(record[3] >= 1 && record[3] <= 2 && record[5] == 0);
    delete app;
    printf("PAINT LIMITS PASS\n");
    return 0;
}
