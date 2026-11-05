// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/wave_out_fixture.c and checks the samples the mixer produced.
//
// The mixer has no host audio device here, so it runs on its own clock and
// writes every mixed frame into the capture buffer this runner hands it. That
// buffer is to a sound test what the framebuffer is to a UI test: the guest
// plays a sine of a known frequency in 8-bit mono at 11.025 kHz, and the check
// is that 16-bit stereo at 44.1 kHz came out with the same frequency, the same
// amplitude and the same value in both channels.
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
// Six seconds of stereo frames: more than the guest ever plays, so nothing is
// lost and the tail stays silent.
static short capture[44100 * 6 * 2];
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "WAVEOUT.EXE"));
    for (unsigned n = 0; n < 2000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "WAVEOUT status=%u code=%u error=%s\n", app.Status(), app.ExitCode(),
                app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    // The guest wrote two blocks of 2205 samples at 11.025 kHz, which is two
    // fifths of a second, and then a second run of the same two blocks that
    // waveOutReset threw away while the stream was paused. What must be in the
    // buffer is one stretch of 441 Hz.
    const unsigned frames = Sound::Captured();
    assert(frames > 44100 / 4);
    unsigned first = 0;
    while (first < frames && !capture[first * 2]) ++first;
    assert(first < frames);
    // The last frame of the sine: after it the buffer is silent again for a
    // while, and the next block (44126) and PlaySound follow later. A sine of
    // 441 Hz never has ten silent milliseconds in it. The next sound comes at
    // least the fixture's Sleep(100) later in real time, but without a host
    // device the mixer renders in bursts on its own clock, and a block written
    // between two bursts starts at the earlier one, so on a loaded host the
    // captured gap came out a few frames short of 100 ms (4382 frames).
    constexpr unsigned Silence = 441;
    unsigned last = first, silence = 0;
    for (unsigned i = first; i < frames && silence < Silence; ++i)
    {
        if (capture[i * 2]) { last = i; silence = 0; }
        else ++silence;
    }
    const unsigned span = last - first + 1;
    if (!(span > 17000 && span < 18000))
    {
        // Where the stretch starts and ends and what follows it, for the diagnosis.
        unsigned next = last + 1;
        while (next < frames && !capture[next * 2]) ++next;
        fprintf(stderr, "WAVEOUT span=%u first=%u last=%u next=%u frames=%u\n", span, first, last, next, frames);
    }
    // Two fifths of a second at 44.1 kHz, within a block of the mixer.
    assert(span > 17000 && span < 18000);
    int peak = 0, crossings = 0;
    short previous = capture[first * 2];
    for (unsigned i = first; i <= last; ++i)
    {
        const short left = capture[i * 2], right = capture[i * 2 + 1];
        assert(left == right);  // a mono source reaches both channels alike
        if (left > peak) peak = left;
        if ((previous < 0) != (left < 0)) ++crossings;
        previous = left;
    }
    // 100 of 128 full scale in eight bit is 25.600 in sixteen; linear
    // interpolation takes a little off the top of a sine.
    assert(peak > 24000 && peak < 26000);
    // 441 Hz over 0,4 seconds is about 352 zero crossings.
    const int expected = 2 * 441 * (int)span / 44100;
    assert(crossings > expected - 4 && crossings < expected + 4);

    // PlaySound played DING.WAV out of the tree after that, synchronously, so
    // there has to be a second stretch of samples behind the silence.
    unsigned later = 0;
    for (unsigned i = last + Silence; i < frames; ++i)
        if (capture[i * 2] || capture[i * 2 + 1]) ++later;
    assert(later > 1000);

    printf("waveOut guest PASS calls=%u frames=%u span=%u peak=%d crossings=%d ding=%u\n",
           app.ApiCalls(), frames, span, peak, crossings, later);
    return 0;
}
