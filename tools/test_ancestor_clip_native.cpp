/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// a child is clipped by the siblings above its parent
// (tools/ancestor_clip_fixture.c). The composition runs where it runs in the
// host, at the guest's wait point.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static void log(const char *) {}

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

static void pump(X86::Application &a, unsigned rounds = 120)
{
    for (unsigned i = 0; i < rounds && a.Status() != X86::Application::Failed &&
                         a.Status() != X86::Application::Exited; ++i)
        a.Pump(8);
    if (a.Status() == X86::Application::Failed || a.Status() == X86::Application::Exited)
    {
        fprintf(stderr, "FAIL state=%d code=%u %s\n", int(a.Status()), a.ExitCode(), a.Error());
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

static unsigned pixel(int x, int y)
{
    const unsigned char *p = User::Pixels() + (size_t(y) * 800 + x) * 4;
    return unsigned(p[2]) << 16 | unsigned(p[1]) << 8 | p[0];
}

static void count(const Gdi::RECT &r, unsigned &blue, unsigned &red, unsigned &yellow)
{
    blue = red = yellow = 0;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x)
        {
            const unsigned c = pixel(x, y);
            blue += c == 0x0000ff;
            red += c == 0xff0000;
            yellow += c == 0xffff00;
        }
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application(log);
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "ANCCLIP.EXE", ""));
    pump(*a);
    const unsigned frame = find("frame");
    assert(frame);
    const unsigned old_view = User::GetWindow(frame, 5);
    assert(old_view);
    Gdi::RECT r{};
    assert(User::GetWindowRect(old_view, &r));
    unsigned blue, red, yellow;
    count(r, blue, red, yellow);
    const unsigned area = unsigned(r.right - r.left) * unsigned(r.bottom - r.top);
    printf("before: area=%u yellow=%u red=%u blue=%u\n", area, yellow, red, blue);
    assert(yellow == area / 2 && red == area / 2);
    assert(User::PostMessage(frame, 0x401, 0, 0));
    pump(*a);
    char title[64];
    User::GetWindowText(frame, title, sizeof title);
    assert(!strcmp(title, "moved"));
    count(r, blue, red, yellow);
    printf("after: area=%u yellow=%u red=%u blue=%u\n", area, yellow, red, blue);
    if (blue != area || red || yellow)
    {
        fprintf(stderr, "FAIL the new view covers %u of %u pixels; the old list shows %u, the old view %u\n",
                blue, area, red, yellow);
        return 1;
    }
    puts("ANCESTOR CLIP PASS");
    return 0;
}
