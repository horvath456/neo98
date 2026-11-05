// SPDX-License-Identifier: GPL-2.0-or-later
// Drives tools/clocks_fixture.c in real time and takes the host wall clock at
// every phase boundary the guest publishes. What the guest measured with
// GetTickCount, timeGetTime and QueryPerformanceCounter has to agree with it.
//
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
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

static constexpr unsigned Records = 0x30000000u, Fields = 6u;
using Clock = std::chrono::steady_clock;

struct Phase
{
    unsigned id = 0, guest_ms = 0, ticks = 0, count = 0, extra = 0, wall_ms = 0;
    // The performance counter in milliseconds, computed here because a 64-bit
    // division in the freestanding guest would need the CRT.
    unsigned counter_ms = 0;
};

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "CLOCKS.EXE"));

    std::vector<Phase> phases;
    unsigned seen = 0;
    auto mark = Clock::now();
    const auto start = mark;
    // The guest measures wall time, so the host has to hand it wall time: one
    // Pump, then a real millisecond of rest whenever nothing is runnable. That
    // is what tools/neo98_host.cpp does between two frames.
    while (app.Status() != X86::Application::Exited && app.Status() != X86::Application::Failed)
    {
        app.Pump(8);
        if (!app.SystemRunnable())
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        unsigned count = 0;
        if (!app.Read(Records, &count, 4) || count == seen)
        {
            if (Clock::now() - start > std::chrono::seconds(120))
                break;
            continue;
        }
        const auto now = Clock::now();
        for (; seen < count; ++seen)
        {
            unsigned f[Fields] = {};
            assert(app.Read(Records + 4 + seen * Fields * 4, f, sizeof f));
            Phase p{f[0], f[1], f[2], f[3], f[4],
                    (unsigned)std::chrono::duration_cast<std::chrono::milliseconds>(now - mark).count(),
                    (unsigned)((unsigned long long)f[2] * 1000ull / 0x1234dcull)};
            phases.push_back(p);
            // Acknowledge, so the guest starts the next phase only now and the
            // wall window of that phase belongs to it alone.
            const unsigned done = 2;
            app.Store(Records + 4 + seen * Fields * 4 + 5 * 4, &done, 4);
        }
        mark = Clock::now();
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "CLOCKS status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    auto find = [&phases](unsigned id) -> const Phase & {
        for (const auto &p : phases)
            if (p.id == id) return p;
        assert(!"phase missing");
        static Phase none;
        return none;
    };
    for (const auto &p : phases)
        printf("CLOCKS phase=%u guest_ms=%u counter_ms=%u count=%u extra=%u wall_ms=%u\n", p.id,
               p.guest_ms, p.counter_ms, p.count, p.extra, p.wall_ms);

    // The performance counter keeps the original frequency of the PIT.
    const Phase &f = find(1);
    assert(f.guest_ms == 0x1234dc && f.ticks == 0);

    // No clock of the guest may report more time than really passed. The wall
    // window of a phase contains the guest window of that phase and the
    // handshake in front of it, so it is the upper bound. The tick count
    // steps with the timer interrupt (16 ms), so a phase can start
    // just before a step and read up to one step more than passed; twenty
    // milliseconds of slack cover that.
    bool okay = true;
    auto within = [&okay](const Phase &p) {
        if (p.guest_ms > p.wall_ms + 20)
        {
            fprintf(stderr, "CLOCKS tick clock runs fast: phase=%u guest_ms=%u wall_ms=%u\n", p.id,
                    p.guest_ms, p.wall_ms);
            okay = false;
        }
        if (p.counter_ms > p.wall_ms + 20)
        {
            fprintf(stderr, "CLOCKS performance counter runs fast: phase=%u counter_ms=%u wall_ms=%u\n",
                    p.id, p.counter_ms, p.wall_ms);
            okay = false;
        }
    };
    unsigned guest_total = 0, counter_total = 0, wall_total = 0;
    for (unsigned id : {10u, 11u, 12u, 20u, 21u, 22u, 23u, 30u, 31u, 32u, 40u, 41u})
    {
        const Phase &p = find(id);
        within(p);
        guest_total += p.guest_ms;
        counter_total += p.counter_ms;
        wall_total += p.wall_ms;
    }
    // The rate over the whole run. A clock that ran fast would show up here
    // even if no single phase did.
    printf("CLOCKS totals guest_ms=%u counter_ms=%u wall_ms=%u\n", guest_total, counter_total, wall_total);
    if (guest_total * 100u < wall_total * 80u || counter_total * 100u < wall_total * 80u)
    {
        fprintf(stderr, "CLOCKS a clock of the guest runs slow against the wall clock\n");
        okay = false;
    }
    assert(okay);

    // The millisecond clock of Win98 (src/win98_system_clock.h).
    // GetTickCount alone steps with the default interrupt of 16 ms (PIT
    // count 4A9h * 16, 15.997 ms, so the steps are 15 or 16 ms).
    const Phase &tick_steps = find(20), &fine_steps = find(22), &fresh_steps = find(23), &bounds = find(24);
    printf("CLOCKS tick_count steps default=%u (largest %u ms) period1=%u (largest %u ms) with_timeGetTime=%u\n",
           tick_steps.count, tick_steps.extra, fine_steps.count, fine_steps.extra, fresh_steps.count);
    assert(tick_steps.extra >= 15 && tick_steps.extra <= 17);
    assert(tick_steps.count >= 20 && tick_steps.count <= 36);
    // After timeBeginPeriod(1) the interrupt comes every millisecond. The
    // interrupt already under way keeps the old count (the PIT loads a new
    // count at its next reload), so one step of 16 ms may open the phase.
    assert(fine_steps.count >= 150 && fine_steps.extra <= 17);
    // timeGetTime updates the cell that GetTickCount reads.
    assert(fresh_steps.count >= 150 && fresh_steps.extra < 15);
    // timeGetTime itself counts single milliseconds (phase 21).
    assert(find(21).count >= 150);
    // VTDAPI's bounds: 0 fails, 200 is accepted both ways, a lone end fails.
    assert(bounds.count == 97 && bounds.guest_ms == 0 && bounds.extra == 97);

    // Every Sleep of the original lasts at least the milliseconds it was given.
    for (unsigned id : {10u, 11u, 12u})
    {
        const Phase &p = find(id);
        const unsigned asked = p.count * p.extra;
        if (p.wall_ms + 2 < asked)
            fprintf(stderr, "CLOCKS sleep too short: phase=%u asked=%u wall_ms=%u\n", p.id, asked, p.wall_ms);
        assert(p.wall_ms + 2 >= asked);
    }

    // WM_TIMER against the wall clock.
    const Phase &fast_timer = find(30);
    const Phase &ten = find(31);
    const Phase &tick = find(32);
    printf("CLOCKS wm_timer per_second 1ms=%u 10ms=%u 55ms=%u\n", fast_timer.count, ten.count, tick.count);
    // A timer never fires more often than its own period allows, with one
    // message of slack for the boundary.
    assert(tick.count <= tick.wall_ms / 55 + 2);
    // USER.EXE only turns a period of zero into one; the list is
    // served from one system timer, and SYSTEM.DRV's
    // CreateSystemTimer raises its rate to at least 37h. A period
    // of one or ten milliseconds therefore fires as often as one of 55.
    assert(ten.count <= ten.wall_ms / 55 + 2);
    assert(fast_timer.count <= fast_timer.wall_ms / 55 + 2);
    assert(fast_timer.count + 4 >= ten.count && ten.count + 4 >= fast_timer.count);
    assert(ten.count + 4 >= tick.count && tick.count + 4 >= ten.count);
    assert(fast_timer.count > 2 && ten.count > 2 && tick.count > 2);

    const Phase &media_fast = find(40);
    const Phase &media_ten = find(41);
    printf("CLOCKS mm_timer per_second 1ms=%u 10ms=%u\n", media_fast.count, media_ten.count);
    // The multimedia timer is the one clock a program may drive at 1 ms, so it
    // may be fast, but never faster than the wall clock allows.
    assert(media_fast.count <= media_fast.wall_ms + 2);
    assert(media_ten.count <= media_ten.wall_ms / 10 + 2);
    assert(media_fast.count > 10 && media_ten.count > 2);

    printf("CLOCKS phases=%zu okay\n", phases.size());
    return 0;
}
