// SPDX-License-Identifier: GPL-2.0-or-later
// the desktop runs in 1024 by 768 (tools/desktop_size_fixture.c).
// The shell composes in the new size, the host frame has that size too (the
// host window follows it), and the pointer reaches a window beyond the old edge.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
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

static void pump(X86::Application &a, unsigned rounds = 200)
{
    for (unsigned i = 0; i < rounds && a.Status() != X86::Application::Failed &&
                         a.Status() != X86::Application::Exited; ++i)
        a.Pump(8);
    if (a.Status() == X86::Application::Failed || a.Status() == X86::Application::Exited)
    {
        fprintf(stderr, "DESKTOP SIZE FAIL state=%d code=%u %s\n", int(a.Status()), a.ExitCode(), a.Error());
        assert(false);
    }
}

static unsigned find(const char *text)
{
    unsigned windows[192], count = 0;
    assert(User::WindowSnapshot(User::GetDesktopWindow(), windows, 192, count));
    for (unsigned i = 0; i < count; ++i)
    {
        char title[64];
        User::GetWindowText(windows[i], title, sizeof title);
        if (!strcmp(title, text)) return windows[i];
    }
    return 0;
}

static unsigned host_pixel(int x, int y)
{
    int width = 0, height = 0;
    User::PixelsSize(width, height);
    const unsigned char *p = User::Pixels() + (size_t(y) * width + x) * 4;
    return unsigned(p[2]) << 16 | unsigned(p[1]) << 8 | p[0];
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application([](const char *t) { if (getenv("DSIZE_LOG")) puts(t); });
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "DSIZE.EXE", ""));
    unsigned window = 0;
    for (unsigned n = 0; n < 40 && !(window = find("size")); ++n) pump(*a, 20);
    assert(window);
    pump(*a);
    int width = 0, height = 0, depth = 0, dw = 0, dh = 0, dd = 0;
    User::ScreenMode(width, height, depth);
    User::DesktopMode(dw, dh, dd);
    Gdi::RECT desk{};
    User::GetWindowRect(User::GetDesktopWindow(), &desk);
    printf("DESKTOP SIZE mode=%dx%dx%d desktop=%dx%dx%d composing=%u desktop_window=%d,%d\n", width, height, depth,
           dw, dh, dd, User::ScreenModeActive() ? 0u : 1u, desk.right, desk.bottom);
    assert(width == 1024 && height == 768 && depth == 32 && dw == 1024 && dh == 768);
    assert(!User::ScreenModeActive() && desk.right == 1024 && desk.bottom == 768);
    // The host frame is 1024x768: the window 850..1000 x 650..750 lies at its
    // own pixels; the rest is the teal of the desktop.
    int frame_width = 0, frame_height = 0;
    User::PixelsSize(frame_width, frame_height);
    const unsigned inside = host_pixel(850, 650), corner = host_pixel(999, 749), outside = host_pixel(849, 649);
    printf("DESKTOP SIZE host frame=%dx%d inside=%06x corner=%06x outside=%06x\n", frame_width, frame_height,
           inside, corner, outside);
    assert(frame_width == 1024 && frame_height == 768);
    assert(inside == 0xff0000 && corner == 0xff0000 && outside == 0x008080);
    // A press beyond the old edge reaches the window.
    a->MouseButtons(900, 700, 0);
    a->MouseButtons(900, 700, 1);
    a->MouseButtons(900, 700, 0);
    pump(*a);
    assert(find("clicked") == window);
    User::PostMessage(window, 0x10, 0, 0);
    for (unsigned n = 0; n < 200 && a->Status() != X86::Application::Exited; ++n) a->Pump(8);
    assert(a->Status() == X86::Application::Exited && a->ExitCode() == 0);
    delete a;
    puts("DESKTOP SIZE PASS");
    return 0;
}
