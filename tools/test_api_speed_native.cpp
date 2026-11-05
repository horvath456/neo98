// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/crash_report.h"
#include "../src/host_microseconds.h"
#include <cassert>
#include <cstring>
#include <string>
#include <cstdio>

struct Text : CrashReport::Sink {
    std::string text;
    bool Write(const void *p, unsigned n) override { text.append(static_cast<const char *>(p), n); return true; }
};
static std::string report(CrashReport::ApiRing &ring, bool json)
{
    auto *r = new CrashReport::Report;
    r->process_count = 1;
    r->processes[0].ring = &ring;
    r->processes[0].thread_count = 1;
    r->processes[0].threads[0].id = 7;
    Text text;
    assert(json ? CrashReport::FormatJson(*r, text) : CrashReport::FormatMarkdown(*r, text));
    delete r;
    return text.text;
}
int main()
{
    CrashReport::ApiRing ordinary, padded;
    // Wrap the ring, leave calls active, and try to finish overwritten calls.
    // Both public report formats must remain byte-for-byte identical.
    for (unsigned i = 0; i < 2300; ++i) {
        unsigned args[16] = {};
        const unsigned count = i % 17;
        for (unsigned j = 0; j < count; ++j) args[j] = i * 13 + j;
        auto a = ordinary.Begin("KERNEL32.DLL", "Test", 7, 0x1234, args, count);
        auto b = padded.Begin<true>("KERNEL32.DLL", "Test", 7, 0x1234, args, count);
        assert(a == b);
        if (i % 3) { ordinary.Finish(a, i); padded.Finish(b, i); }
        if (i > 1024) { ordinary.Finish(i - 1024, 999); padded.Finish(i - 1024, 999); }
        const auto *x = ordinary.Recent(0), *y = padded.Recent(0);
        assert(x && y && x->argc == y->argc && x->value == y->value && x->returned == y->returned);
        assert(!memcmp(x->args, y->args, sizeof x->args));
    }
    assert(report(ordinary, true) == report(padded, true));
    assert(report(ordinary, false) == report(padded, false));
    ordinary.Begin("KERNEL32.DLL", "Missing", 7, 0, nullptr, 0);
    assert(ordinary.Recent(0)->args[0] == 0);
#if defined(__APPLE__)
    static_assert(HostClock::TickDivisor(125, 3) == 24);
    static_assert(HostClock::TickDivisor(1, 1) == 1000);
    static_assert(HostClock::TickDivisor(7, 1) == 0);
    static_assert(HostClock::TickDivisor(0, 1) == 0);
    for (unsigned i = 0; i < 100000; ++i) {
        auto before = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000;
        auto value = HostClock::Microseconds();
        auto after = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000;
        assert(before <= value && value <= after);
    }
#endif
    puts("PASS API ring reports, wraparound, nested calls and host clock equivalence");
}
