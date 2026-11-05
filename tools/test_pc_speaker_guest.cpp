// SPDX-License-Identifier: GPL-2.0-or-later
// Run pc_speaker_fixture.c and inspect captured mixer samples without a host
// audio device. Check the 1000-Hz port-driven tone and the 748-Hz MessageBeep
// tone as separate segments.
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
static short capture[44100 * 4 * 2];

// A tone segment records start, duration, peak level and zero crossings. gap
// silent frames end it.
struct Stretch
{
    unsigned first = 0, span = 0;
    int peak = 0, crossings = 0;
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
        if (left > out.peak) out.peak = left;
        if ((previous < 0) != (left < 0)) ++out.crossings;
        previous = left;
    }
    return true;
}
// Calculate square-wave frequency from zero crossings.
static double Hertz(const Stretch &s)
{
    return s.span ? s.crossings * (double)Sound::OutputRate / (2.0 * s.span) : 0.0;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "SPEAKER.EXE"));
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "SPEAKER status=%u code=%u error=%s\n", app.Status(), app.ExitCode(),
                app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    const unsigned frames = Sound::Captured();
    // The guest waits 900 ms, so the mixer must produce at least half a
    // second of output.
    assert(frames > Sound::OutputRate / 2);
    // The first segment is port-driven. Preceding silence occurs after
    // loading the divisor but before opening the gate.
    Stretch gate;
    assert(Find(0, frames, Sound::OutputRate / 20, gate));
    const double gate_hertz = Hertz(gate);
    // The second section is MessageBeep, following the first section's
    // silence.
    Stretch beep;
    assert(Find(gate.first + gate.span + Sound::OutputRate / 20, frames, Sound::OutputRate / 20, beep));
    const double beep_hertz = Hertz(beep);
    printf("PC SPEAKER frames=%u gate first=%u span=%u peak=%d hertz=%.1f "
           "beep first=%u span=%u peak=%d hertz=%.1f\n",
           frames, gate.first, gate.span, gate.peak, gate_hertz, beep.first, beep.span, beep.peak,
           beep_hertz);
    // The counter tone is 1,193,182/1193 = 1000.2 Hz.
    assert(gate_hertz > 960 && gate_hertz < 1040);
    assert(gate.peak > 0x800);
    // The guest waits 300 ms of wall time, so tolerate timing variation. Less
    // than half that duration is insufficient.
    assert(gate.span > Sound::OutputRate / 10);
    // MessageBeep produces 748 Hz for forty milliseconds.
    assert(beep_hertz > 710 && beep_hertz < 790);
    assert(beep.span > Sound::OutputRate / 50 && beep.span < Sound::OutputRate / 10);
    assert(beep.peak > 0x800);
    // After the beep, the closed gate keeps the counter silent.
    Stretch rest;
    assert(!Find(beep.first + beep.span + Sound::OutputRate / 20, frames, Sound::OutputRate / 20,
                 rest));
    printf("PC SPEAKER PASS\n");
    return 0;
}
