// SPDX-License-Identifier: GPL-2.0-or-later
// Run the SNDVOICE fixture and inspect MMSOUND note-queue audio. Without a
// host audio device, capture mixer output on its own clock. The guest checks
// SOUND gate results; this runner checks frequency, duration, rests,
// StopSound during a note and silence after task exit without CloseSound.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
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
static short capture[44100 * 12 * 2];

// A tone segment records start, duration and zero crossings. Speaker samples
// are nonzero while active; gap silent frames end a segment.
struct Stretch
{
    unsigned first = 0, span = 0;
    int crossings = 0;
};
static bool Find(unsigned from, unsigned frames, unsigned gap, Stretch &out)
{
    unsigned first = from;
    while (first < frames && !capture[first * 2]) ++first;
    if (first >= frames) return false;
    unsigned last = first, silence = 0;
    for (unsigned i = first; i < frames && silence < gap; ++i)
    {
        if (capture[i * 2]) { last = i; silence = 0; }
        else ++silence;
    }
    out = Stretch{};
    out.first = first;
    out.span = last - first + 1;
    short previous = capture[first * 2];
    for (unsigned i = first; i <= last; ++i)
    {
        const short left = capture[i * 2];
        if ((previous < 0) != (left < 0)) ++out.crossings;
        previous = left;
    }
    return true;
}
static double Hertz(const Stretch &s)
{
    return s.span ? s.crossings * (double)Sound::OutputRate / (2.0 * s.span) : 0.0;
}
static double Ms(unsigned frames) { return frames * 1000.0 / Sound::OutputRate; }
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "SNDVOICE.EXE"));
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    printf("SNDVOICE status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    // The exit code is the number of the first answer that was not the one of
    // MMSOUND.DRV (tools/win16_fixture.py, voice_segment).
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    // After task exit, advance the mixer another 600 ms. The unfinished
    // two-second note must remain silent.
    app.Pump(1);
    const auto after = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - after < std::chrono::milliseconds(600))
    {
        Sound::Shared().Advance(X86::Application::Now());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const unsigned frames = Sound::Captured(), gap = Sound::OutputRate / 50; // 20 ms
    struct Expected
    {
        const char *name;
        double hertz, ms, ms_tolerance, gap_ms;
    };
    // Durations use five-millisecond callbacks. Rests take 13 ticks after the
    // normal note, 25 after staccato and 1+100 for a quarter rest.
    // SetVoiceSound halves its duration, so forty ticks produce twenty
    // callbacks. E and F depend on the program's 300-ms wall-clock interval.
    const Expected expected[] = {
        {"A normal 440 Hz", 440.1, 440, 6, -1},   {"B staccato 880 Hz", 880.6, 375, 6, 65},
        {"C legato 659 Hz", 659.2, 250, 6, 125},  {"D legato after a rest 440 Hz", 440.1, 500, 6, 505},
        {"G SetVoiceSound 1000 Hz", 1000.2, 100, 6, -1},
        {"E StopSound 440 Hz", 440.1, 300, 150, -1},
        {"F task end 880 Hz", 880.6, 300, 150, -1},
    };
    unsigned at = 0, previous_end = 0;
    for (const auto &want : expected)
    {
        Stretch s;
        assert(Find(at, frames, gap, s));
        const double hertz = Hertz(s), ms = Ms(s.span), gap_ms = previous_end ? Ms(s.first - previous_end) : 0;
        printf("SNDVOICE %s: first=%u %.1f ms %.1f Hz, gap before %.1f ms\n", want.name, s.first, ms, hertz,
               gap_ms);
        assert(hertz > want.hertz * 0.98 && hertz < want.hertz * 1.02);
        assert(ms > want.ms - want.ms_tolerance && ms < want.ms + want.ms_tolerance);
        if (want.gap_ms >= 0) assert(gap_ms > want.gap_ms - 6 && gap_ms < want.gap_ms + 6);
        previous_end = s.first + s.span;
        at = previous_end;
    }
    // No sound remains after the task exits.
    Stretch rest;
    assert(!Find(at, frames, gap, rest));
    assert(Ms(frames - at) > 400);
    printf("SNDVOICE frames=%u silent tail %.1f ms\n", frames, Ms(frames - at));
    printf("SNDVOICE PASS\n");
    return 0;
}
