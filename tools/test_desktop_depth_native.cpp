// SPDX-License-Identifier: GPL-2.0-or-later
// 256 colours as the stored display mode, across a restart.
//
// Two runs of this runner share one registry journal, the way the host shares
// it across its re-exec after ExitWindows. The first (STORE) lets the guest
// store 8 bits with DESK.CPL's CDS_UPDATEREGISTRY | CDS_NORESET; the screen
// stays at 32 bits. The second (CHECK) starts like the machine after the
// restart: the display comes up in the registry mode, the shell composes in
// it, and the frame on the host is the 8-bit frame, every pixel a colour of
// the system palette. At the end the guest goes back to 32 bits without a
// restart.
#include "../src/x86_app.h"
#include "registry_file.h"
#include "../src/win98_dib_dither.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, size, file) == size_t(size));
    fclose(file);
    return bytes;
}

static bool live(X86::Application &a)
{
    return a.Status() != X86::Application::Failed && a.Status() != X86::Application::Exited;
}

static void pump(X86::Application &a, unsigned rounds)
{
    for (unsigned i = 0; i < rounds && live(a); ++i) a.Pump(8);
    if (a.Status() == X86::Application::Failed || (a.Status() == X86::Application::Exited && a.ExitCode()))
    {
        fprintf(stderr, "DESKTOP DEPTH FAIL code=%u %s\n", a.ExitCode(), a.Error() ? a.Error() : "");
        assert(false);
    }
}

// The published host frame, BGRA, 800 by 600.
static unsigned host_pixel(int x, int y)
{
    const unsigned char *p = User::Pixels() + (size_t(y) * 800 + x) * 4;
    return unsigned(p[2]) << 16 | unsigned(p[1]) << 8 | p[0];
}

static unsigned find_window(const char *title)
{
    unsigned windows[192], count = 0;
    assert(User::WindowSnapshot(User::GetDesktopWindow(), windows, 192, count));
    for (unsigned i = 0; i < count; ++i)
    {
        char text[64] = {};
        User::GetWindowText(windows[i], text, sizeof text);
        if (!strcmp(text, title)) return windows[i];
    }
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    const bool store = !strcmp(argv[4], "STORE");
    char *attach[] = {argv[0], (char *)"--registry", argv[3]};
    assert(RegistryFile::Attach(3, attach, nullptr));
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "DEPTH.EXE", store ? "DEPTH STORE" : "DEPTH CHECK"));
    int width = 0, height = 0, depth = 0;
    if (store)
    {
        for (unsigned n = 0; n < 4000 && live(*app); ++n) pump(*app, 1);
        assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
        User::ScreenMode(width, height, depth);
        printf("DESKTOP DEPTH STORE mode=%dx%dx%d\n", width, height, depth);
        assert(width == 800 && height == 600 && depth == 32);
        delete app;
        printf("DESKTOP DEPTH STORE PASS\n");
        return 0;
    }
    unsigned window = 0;
    for (unsigned n = 0; n < 4000 && live(*app) && !(window = find_window("depth")); ++n) pump(*app, 1);
    assert(window);
    pump(*app, 200);
    User::ScreenMode(width, height, depth);
    int desktop_width = 0, desktop_height = 0, desktop_depth = 0;
    User::DesktopMode(desktop_width, desktop_height, desktop_depth);
    printf("DESKTOP DEPTH CHECK mode=%dx%dx%d desktop=%dx%dx%d composing=%u\n", width, height, depth,
           desktop_width, desktop_height, desktop_depth, User::ScreenModeActive() ? 0u : 1u);
    assert(width == 800 && height == 600 && depth == 8);
    assert(desktop_depth == 8 && !User::ScreenModeActive());
    // The screen surface holds palette indices; the host frame shows the
    // palette's colours. Olive is entry 3 of the static colours; RGB(200,100,50)
    // is no entry, and DIBENG rasters it into an 8x8 pattern of static colours
    // (Win98DibDither); the desktop is teal.
    const unsigned char *bits = User::ScreenBits();
    const unsigned *colors = User::ScreenColors();
    assert(bits && colors && User::ScreenStride() == 800);
    const unsigned left = bits[150 * 800 + 150], right = bits[130 * 800 + 230], desktop = bits[20 * 800 + 700];
    printf("DESKTOP DEPTH CHECK indices left=%u right=%u desktop=%u host=%06x,%06x,%06x\n", left, right, desktop,
           host_pixel(150, 150), host_pixel(230, 130), host_pixel(700, 20));
    assert(colors[left] == 0x808000 && colors[desktop] == 0x008080);
    assert(host_pixel(150, 150) == 0x808000 && host_pixel(700, 20) == 0x008080);
    (void)right;
    unsigned char pattern[64];
    Win98DibDither::Pattern(0x003264c8u, pattern);
    bool aligned = false;
    for (unsigned shift = 0; shift < 64 && !aligned; ++shift)
    {
        aligned = true;
        for (int y = 0; y < 16 && aligned; ++y)
            for (int x = 0; x < 16 && aligned; ++x)
                aligned = bits[(120 + y) * 800 + 220 + x] ==
                          pattern[((y + int(shift / 8)) & 7) * 8 + ((x + int(shift % 8)) & 7)];
    }
    printf("DESKTOP DEPTH CHECK dithered=%u\n", aligned ? 1u : 0u);
    assert(aligned);
    // Every pixel of the host frame is a colour of the system palette.
    unsigned outside = 0;
    for (int y = 0; y < 600; y += 3)
        for (int x = 0; x < 800; x += 3)
        {
            const unsigned value = host_pixel(x, y);
            bool found = false;
            for (unsigned i = 0; i < 256 && !found; ++i) found = colors[i] == value;
            outside += !found;
        }
    printf("DESKTOP DEPTH CHECK outside_palette=%u\n", outside);
    assert(outside == 0);
    User::PostMessage(window, 0x10, 0, 0);
    for (unsigned n = 0; n < 4000 && live(*app); ++n) pump(*app, 1);
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    User::ScreenMode(width, height, depth);
    User::DesktopMode(desktop_width, desktop_height, desktop_depth);
    printf("DESKTOP DEPTH CHECK after mode=%dx%dx%d desktop depth=%d\n", width, height, depth, desktop_depth);
    assert(depth == 32 && desktop_depth == 32 && !User::ScreenModeActive());
    delete app;
    printf("DESKTOP DEPTH CHECK PASS\n");
    return 0;
}
