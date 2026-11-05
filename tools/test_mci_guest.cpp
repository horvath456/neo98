// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/mci_fixture.c and checks the samples MCI produced.
//
// The mixer has no host audio device here, so it runs on its own clock and
// writes every mixed frame into the capture buffer this runner hands it. The
// guest plays a wave file of the tree through the waveaudio device and then a
// standard MIDI file through the sequencer, and the sequencer's note is 69,
// which is 440 Hz; that is the sample side of a pixel check.
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
struct Stretch
{
    unsigned first = 0, span = 0;
    int peak = 0, crossings = 0;
};
static bool Find(unsigned from, unsigned frames, unsigned gap, Stretch &out)
{
    unsigned first = from;
    while (first < frames && !capture[first * 2] && !capture[first * 2 + 1]) ++first;
    if (first >= frames) return false;
    unsigned last = first, silence = 0;
    for (unsigned i = first; i < frames && silence < gap; ++i)
    {
        if (capture[i * 2] || capture[i * 2 + 1]) { last = i; silence = 0; }
        else ++silence;
    }
    out.first = first;
    out.span = last - first + 1;
    out.peak = 0;
    out.crossings = 0;
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
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "MCI.EXE"));
    for (unsigned n = 0; n < 6000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "MCI status=%u code=%u error=%s\n", app.Status(), app.ExitCode(),
                app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    const unsigned frames = Sound::Captured();
    assert(frames > 44100);
    // The waveaudio stretches come first: the guest played DING.WAV four
    // times, so there is sound well before the half second of silence it left
    // in front of the sequencer.
    Stretch wave;
    assert(Find(0, frames, 44100 / 10, wave));
    assert(wave.span > 4410);
    assert(wave.peak > 1000);

    // The sequencer is the last stretch, and its note is 440 Hz.
    unsigned at = wave.first + wave.span;
    Stretch last = wave, current;
    while (Find(at, frames, 44100 / 4, current))
    {
        last = current;
        at = current.first + current.span + 1;
    }
    assert(last.span > 44100 / 8);
    assert(last.peak > 1000);
    const int expected = 2 * 440 * (int)last.span / 44100;
    assert(last.crossings > expected - 6 && last.crossings < expected + 6);

    printf("MCI guest PASS calls=%u frames=%u wave=%u/%d sequence=%u/%d/%d\n", app.ApiCalls(),
           frames, wave.span, wave.peak, last.span, last.peak, last.crossings);
    return 0;
}
