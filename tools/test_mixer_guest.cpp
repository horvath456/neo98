// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/mixer_fixture.c and checks that the mixer controls moved the
// sound.
// The guest walks the mixer device and then plays the same square wave twice:
// once with the destination line at full volume and once at half. The mixer
// has no host audio device here, so it runs on its own clock and writes every
// mixed frame into the capture buffer this runner hands it. The check is that
// the second stretch is half as loud as the first, which is the sound side of
// a pixel check: a control that only remembered its value would leave both
// stretches equally loud.
//
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
// Eight seconds of stereo frames: the guest plays two stretches of 0,8 s with
// a pause between them and never reaches the end of this buffer.
static short capture[44100 * 8 * 2];

// The peak of one stretch of sound, and where it ended. `from` is the first
// frame to look at; a stretch ends after a tenth of a second of silence.
static int PeakOfStretch(unsigned frames, unsigned from, unsigned &begin, unsigned &end)
{
    unsigned first = from;
    while (first < frames && !capture[first * 2]) ++first;
    if (first >= frames) return -1;
    unsigned last = first, silence = 0;
    for (unsigned i = first; i < frames && silence < 4410; ++i)
    {
        if (capture[i * 2]) { last = i; silence = 0; }
        else ++silence;
    }
    int peak = 0;
    for (unsigned i = first; i <= last; ++i)
    {
        const short left = capture[i * 2], right = capture[i * 2 + 1];
        assert(left == right);   // a mono source reaches both channels alike
        if (left > peak) peak = left;
    }
    begin = first;
    end = last;
    return peak;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "MIXER.EXE"));
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "MIXER status=%u code=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    const unsigned frames = Sound::Captured();
    assert(frames > 44100);
    unsigned begin = 0, end = 0;
    const int loud = PeakOfStretch(frames, 0, begin, end);
    assert(loud > 0);
    unsigned quiet_begin = 0, quiet_end = 0;
    const int quiet = PeakOfStretch(frames, end + 4410, quiet_begin, quiet_end);
    assert(quiet > 0);
    // 0E0h of 128 full scale in eight bit is 24.576 in sixteen; the square
    // wave keeps its top, so the peak is that value.
    assert(loud > 23000 && loud < 26000);
    // The destination line stood at 4000h of 0FFFFh, so a quarter.
    const int expected = loud / 4;
    assert(quiet > expected * 9 / 10 && quiet < expected * 11 / 10);
    printf("MIXER PASS loud=%d quiet=%d ratio=%d%%\n", loud, quiet, quiet * 100 / loud);
    return 0;
}
