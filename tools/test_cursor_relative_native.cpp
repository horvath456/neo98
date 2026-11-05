// SPDX-License-Identifier: GPL-2.0-or-later
// Feeds absolute pointer positions the way a window system does and checks the
// steps a guest reads that puts the cursor back on the centre after every
// movement. That is what the mouse emulation of the original DINPUT does, and
// is the movement it read being far larger than the movement the user
// made. The runner stands in for the host window: it keeps a pointer position,
// moves it by ten pixels per event, and follows the guest's SetCursorPos when
// the provider of tools/neo98_host.cpp is installed.
//
// A third run starts that stand-in pointer far from the centre instead of on
// it, the way the real host pointer sits wherever the user's hand left it
// when a program first reaches its own SetCursorPos call. The pointer-alignment check asks
// whether that gap survives as an uncorrected bias in the sum of the steps
// the guest reads (an integration constant); it does not (.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
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
// The pointer of the stand-in window system.
static int pointer_x = 400, pointer_y = 300;

struct Run
{
    std::vector<int> dx, dy;
};

static Run once(const std::vector<unsigned char> &exe, const std::vector<unsigned char> &font,
                bool follow, int start_x = 400, int start_y = 300)
{
    pointer_x = start_x;
    pointer_y = start_y;
    if (follow)
        X86::Application::SetHostCursorProvider(nullptr, [](void *, int x, int y) {
            pointer_x = x;
            pointer_y = y;
        });
    else
        X86::Application::SetHostCursorProvider(nullptr, nullptr);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "CURSOR.EXE"));
    // Let the guest reach its message loop.
    for (unsigned n = 0; n < 4000; ++n)
    {
        app.Pump(8);
        unsigned ready = 0;
        if (app.Read(Records + 129 * 4, &ready, 4) && ready) break;
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Four movements of ten pixels to the right, the way a user drags the
    // mouse. The runner keeps its own schedule: feed one movement, wait until
    // the guest has read it, then let it work through its centring before the
    // next one. Win98 folds WM_MOUSEMOVE messages the program has not read yet
    // into one, so a fixed number of slices made the steps depend on the load
    // of the machine.
    for (unsigned step = 0; step < 4; ++step)
    {
        unsigned before = 0;
        assert(app.Read(Records, &before, 4));
        pointer_x += 10;
        app.MouseButtons(pointer_x, pointer_y, 0);
        for (unsigned n = 0, read = 0, settle = 0; n < 200000 && settle < 600; ++n)
        {
            app.Pump(8);
            if (app.Status() == X86::Application::Failed) break;
            unsigned now = before;
            if (!read && app.Read(Records, &now, 4) && now != before) read = 1;
            if (read) ++settle;
            if (app.Status() == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (app.Status() == X86::Application::Failed) break;
    }
    if (app.Status() == X86::Application::Failed)
        fprintf(stderr, "CURSOR failed: %s\n", app.Error());
    assert(app.Status() != X86::Application::Failed);
    unsigned count = 0;
    assert(app.Read(Records, &count, 4));
    fprintf(stderr, "CURSOR run follow=%u status=%u callbacks=%u calls=%u moves=%u\n", follow ? 1u : 0u,
            (unsigned)app.Status(), app.Callbacks(), app.ApiCalls(), count);
    // Only the movements the guest would report to a program. The zero steps
    // are the moves its own centring produced; the original reads them and
    // reports nothing.
    Run r;
    for (unsigned i = 0; i < count && i < 64; ++i)
    {
        int step[2] = {};
        assert(app.Read(Records + 4 + i * 8, step, 8));
        if (!step[0] && !step[1])
            continue;
        r.dx.push_back(step[0]);
        r.dy.push_back(step[1]);
    }
    return r;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());

    const Run following = once(exe, font, true);
    const Run standing = once(exe, font, false);
    // Check the initial offset: a host pointer that is far from the centre
    // when the guest first calls SetCursorPos leaves an uncorrected bias in
    // the sum of the steps the guest reads, the way an integration constant
    // would. Start the stand-in host pointer at a corner of the screen
    // instead of the centre and repeat the same four movements: if the
    // SetCursorPos-follow logic itself carried such a bias, the sum of the
    // steps the guest reads would differ from the sum of the real movements
    // fed in (40). It does not, so the constant offset is not this
    // mechanism (.
    const Run far = once(exe, font, true, 30, 570);
    printf("CURSOR following");
    for (int d : following.dx) printf(" %d", d);
    printf("\nCURSOR standing ");
    for (int d : standing.dx) printf(" %d", d);
    printf("\nCURSOR far");
    for (int d : far.dx) printf(" %d", d);
    printf("\n");
    // All four injected movements must arrive, independently of the number
    // of synthetic zero movements produced by SetCursorPos.
    assert(following.dx.size() == 4 && standing.dx.size() == 4);
    assert(far.dx.size() == 4);
    {
        int sum = 0;
        for (int d : far.dx) sum += d;
        const int expected = 10 * (int)far.dx.size();
        if (sum != expected)
            fprintf(stderr, "CURSOR far sum=%d expected=%d (pointer started away from the centre)\n",
                    sum, expected);
        assert(sum == expected);
        for (int d : far.dy) assert(d == 0);
    }

    // With the host pointer following SetCursorPos, every movement of ten
    // pixels is read as ten pixels.
    for (size_t i = 0; i < following.dx.size(); ++i)
    {
        if (following.dx[i] != 10 || following.dy[i] != 0)
            fprintf(stderr, "CURSOR step %zu is %d,%d and not 10,0\n", i, following.dx[i],
                    following.dy[i]);
        assert(following.dx[i] == 10 && following.dy[i] == 0);
    }
    // Without it the guest reads the distance between the centre it moved the
    // cursor to and the place the host pointer has reached. That distance grows
    // with every movement: the first one is the real step, because the pointer
    // started on the centre, and from the second one on the gap left by the
    // previous centring adds up (10, 20, 30, 40). That causes pointer drift.
    // Before the runner waited for each read, folded movements made
    // the first step look twice as large.
    for (size_t i = 0; i < standing.dx.size(); ++i)
    {
        assert(standing.dy[i] == 0);
        if (i)
            assert(standing.dx[i] > standing.dx[i - 1] && standing.dx[i] > 10);
        else
            assert(standing.dx[i] == 10);
    }

    printf("CURSOR relative movement steps %zu, following the host pointer gives the real step\n",
           following.dx.size());
    return 0;
}
