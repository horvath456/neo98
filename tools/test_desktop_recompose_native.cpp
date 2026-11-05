/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// Two applications share a desktop. After one exits, its windows disappear
// and normal composition must restore the exposed desktop. Exercise
// ComposeIfQuiet from Application::Suspend; do not bypass that path with an
// explicit Render call.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void log(const char *text) { puts(text); }

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

static void check(X86::Application &a)
{
    if (a.Status() == X86::Application::Failed ||
        (a.Status() == X86::Application::Exited && a.ExitCode()))
    {
        fprintf(stderr, "FAIL pid=%u code=%u %s\n", a.ProcessId(), a.ExitCode(), a.Error());
        assert(false);
    }
}

static void pump(X86::Application &a, unsigned rounds = 80)
{
    for (unsigned i = 0; i < rounds && live(a); ++i) a.Pump(8);
    check(a);
}

static void pump2(X86::Application &a, X86::Application &b, unsigned rounds = 160)
{
    for (unsigned i = 0; i < rounds && live(a) && live(b); ++i) { a.Pump(8); b.Pump(8); }
    check(a);
    check(b);
}

static unsigned window(unsigned pid, const char *text)
{
    unsigned windows[192], count = 0;
    assert(User::WindowSnapshot(User::GetDesktopWindow(), windows, 192, count));
    for (unsigned i = 0; i < count; ++i)
    {
        char title[256];
        User::GetWindowText(windows[i], title, 256);
        if (User::WindowProcess(windows[i]) == pid && !strcmp(title, text)) return windows[i];
    }
    return 0;
}

// Published pixels are BGRA at 800 by 600.
static unsigned pixel(int x, int y)
{
    const unsigned char *bits = User::Pixels();
    assert(bits);
    const unsigned char *p = bits + (size_t(y) * 800 + x) * 4;
    return unsigned(p[2]) << 16 | unsigned(p[1]) << 8 | p[0];
}

static unsigned count_colour(int left, int top, int right, int bottom, unsigned colour)
{
    unsigned n = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (pixel(x, y) == colour) ++n;
    return n;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    const unsigned desktop = 0x008080, window_colour = 0xffffff;
    // The second window's client area, inset from the frame.
    const int left = 320, top = 160, right = 540, bottom = 280;
    const unsigned area = unsigned(right - left) * unsigned(bottom - top);

    auto *a = new X86::Application(log);
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "RECOMP.EXE", "A"));
    pump(*a);
    const unsigned first = window(a->ProcessId(), "A");
    assert(first);
    assert(count_colour(left, top, right, bottom, desktop) == area);

    // Test exit from the message loop and exit inside WM_PAINT between
    // BeginPaint and EndPaint.
    unsigned covered = 0, again = 0;
    for (unsigned round = 0; round < 2; ++round)
    {
        auto *b = new X86::Application(log);
        assert(b->Start(exe.data(), exe.size(), font.data(), font.size(), "RECOMP.EXE", "B"));
        pump2(*a, *b);
        const unsigned second = window(b->ProcessId(), "B");
        assert(second && User::IsWindow(first) && a->ProcessId() != b->ProcessId());
        covered = count_colour(left, top, right, bottom, window_colour);
        if (covered != area)
        {
            fprintf(stderr, "FAIL round %u: the second window covers only %u of %u pixels\n",
                    round, covered, area);
            assert(false);
        }
        assert(User::PostMessage(second, round ? 0x402 : 0x401, 0, 0));
        pump(*b);
        assert(b->Status() == X86::Application::Exited && b->ExitCode() == 0);
        delete b;
        assert(!User::IsWindow(second) && User::IsWindow(first));

        // The surviving process must trigger composition at its normal wait
        // point.
        pump(*a);
        again = count_colour(left, top, right, bottom, desktop);
        const unsigned left_over = count_colour(left, top, right, bottom, window_colour);
        if (again != area || left_over)
        {
            fprintf(stderr,
                    "FAIL round %u: the area of the closed process still shows %u of %u pixels of "
                    "its window; only %u of %u are the desktop again\n",
                    round, left_over, area, again, area);
            assert(false);
        }
    }

    assert(User::PostMessage(first, 0x401, 0, 0));
    pump(*a);
    assert(a->Status() == X86::Application::Exited && a->ExitCode() == 0);
    delete a;
    assert(!User::LiveWindows());
    printf("DESKTOP RECOMPOSE PASS area=%u covered=%u desktop_again=%u\n", area, covered, again);
}
