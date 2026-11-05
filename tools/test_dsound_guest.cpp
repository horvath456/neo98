// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/dsound_fixture.c over the original DSOUND.DLL of the tree and
// checks the samples that came out of the mixer.
// DirectSound of Windows 98 reaches a card with a DirectSound driver through
// DSOUND.VXD: a sound buffer is memory the card plays where it lies. This
// port is that card (src/x86_dsound_vxd.cpp), and its mixer plays the buffer,
// so everything the guest wrote has to appear in the capture buffer of
// src/sound_mixer.cpp, and the check is the same one the wave output test
// makes: the 441 Hz the guest wrote into its secondary buffer must come out
// as 441 Hz.
//
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
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
// Ten seconds of stereo frames. The primary buffer runs the whole time, so the
// capture holds far more than the sound itself.
static short capture[44100 * 10 * 2];
static std::string debug_output;
static void collect(const char *text)
{
    debug_output += text;
    debug_output += '\n';
}
static unsigned observation(const char *name, bool &found)
{
    const std::string key = std::string("DSOUND ") + name + "=";
    const auto at = debug_output.find(key);
    found = at != std::string::npos;
    if (!found) return 0;
    return (unsigned)strtoul(debug_output.c_str() + at + key.size(), nullptr, 16);
}
static unsigned expect(const char *name, unsigned want)
{
    bool found = false;
    const unsigned got = observation(name, found);
    if (!found || got != want)
        fprintf(stderr, "DSOUND %s=%08x, expected %08x%s\n", name, got, want,
                found ? "" : " (missing)");
    assert(found && got == want);
    return got;
}
static unsigned at_least(const char *name, unsigned want)
{
    bool found = false;
    const unsigned got = observation(name, found);
    if (!found || got < want)
        fprintf(stderr, "DSOUND %s=%u, expected at least %u%s\n", name, got, want,
                found ? "" : " (missing)");
    assert(found && got >= want);
    return got;
}
static unsigned below(const char *name, unsigned limit)
{
    bool found = false;
    const unsigned got = observation(name, found);
    if (!found || got >= limit)
        fprintf(stderr, "DSOUND %s=%u, expected below %u%s\n", name, got, limit,
                found ? "" : " (missing)");
    assert(found && got < limit);
    return got;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    app.log = collect;
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "DSOUND.EXE"));
    for (unsigned n = 0; n < 8000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "DSOUND status=%u code=%u error=%s\n%s\n", app.Status(), app.ExitCode(),
                app.Error(), debug_output.c_str());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    // The contract of the run. Every one of these is an answer of the original
    // DSOUND.DLL, not of this port.
    expect("loaded", 1);
    expect("create_found", 1);
    expect("create", 0);
    expect("object", 1);
    expect("getcaps", 0);
    // No DSCAPS_EMULDRIVER: DSOUND found its VxD and the DirectSound driver
    // behind it (src/x86_dsound_vxd.cpp), so it does not go through the wave
    // device. DSCAPS_CERTIFIED (40h) is the checksum of the driver's module
    // name, which DSOUND.DLL works out itself.
    expect("caps_emulated", 0);
    expect("caps_flags", 0xf5f);
    expect("window", 1);
    expect("cooperative", 0);
    expect("primary", 0);
    expect("primary_object", 1);
    expect("primary_format", 0);
    expect("primary_play", 0);
    expect("secondary", 0);
    expect("secondary_object", 1);
    expect("lock", 0);
    expect("lock_bytes", 4410);
    expect("unlock", 0);
    expect("volume", 0);
    expect("pan", 0);
    expect("frequency", 0);
    expect("volume_back", 0);
    expect("play", 0);
    expect("stop", 0);

    // The play cursor of a looping buffer. A streaming engine waits for it:
    // SDL 1.2's DirectSound thread asks GetCurrentPosition in a loop and only
    // writes the next chunk once the cursor has left the one it holds, and the
    // engine of Super Collapse does the same on its own thread. A cursor that
    // stands still, that never wraps or that leaves its buffer turns that wait
    // into an endless loop, which is why these five numbers are measured.
    expect("loop_buffer", 0);
    expect("loop_object", 1);
    expect("loop_lock", 0);
    expect("loop_rewind", 0);
    expect("loop_play", 0);
    // DSBSTATUS_PLAYING 1 and DSBSTATUS_LOOPING 4.
    expect("loop_status", 5);
    expect("loop_position", 0);
    // Fifty readings, forty milliseconds apart, over a buffer of two fifths of
    // a second: the run must reach all fifty, so the cursor never left the
    // buffer and GetCurrentPosition never failed.
    expect("loop_steps", 50);
    // Two seconds of a buffer that holds four tenths: the cursor moves at every
    // reading and turns over four times. The bands are wide because the host
    // shares the machine; what they exclude is a cursor that does not move.
    at_least("loop_advances", 15);
    at_least("loop_wraps", 2);
    below("loop_highest", 4410);
    // The cursor may move in steps, because DirectSound mixes the secondary
    // buffer into the primary one block at a time, but it must never stand
    // still long enough to look stopped: eight readings are a third of a
    // second and the buffer itself is only four tenths long.
    below("loop_longest_stall", 8);
    // The distance the cursor covered against the time that passed on the
    // clock of the guest. Eight bit mono at 11.025 kHz plays 11.025 bytes a
    // millisecond, so the two numbers have to meet within a quarter; a cursor
    // that crawls or races is the one that leaves a streaming engine waiting.
    {
        bool have_travelled = false, have_expected = false;
        const unsigned travelled = observation("loop_travelled", have_travelled);
        const unsigned wanted = observation("loop_expected", have_expected);
        assert(have_travelled && have_expected && wanted > 4410);
        if (travelled * 4 < wanted * 3 || travelled * 3 > wanted * 4)
            fprintf(stderr, "DSOUND loop_travelled=%u, expected near %u\n", travelled, wanted);
        assert(travelled * 4 >= wanted * 3 && travelled * 3 <= wanted * 4);
    }
    expect("loop_status_end", 5);
    expect("loop_stop", 0);
    expect("done", 1);

    // The samples. The primary buffer writes silence the whole time it runs, so
    // the sound is the stretch between the first and the last frame that is not
    // zero.
    const unsigned frames = Sound::Captured();
    assert(frames > 44100 / 4);
    unsigned first = 0;
    while (first < frames && !capture[first * 2] && !capture[first * 2 + 1]) ++first;
    assert(first < frames);
    unsigned last = first, silence = 0;
    for (unsigned i = first; i < frames && silence < 4410; ++i)
    {
        if (capture[i * 2] || capture[i * 2 + 1]) { last = i; silence = 0; }
        else ++silence;
    }
    const unsigned span = last - first + 1;
    unsigned quiet = 0;
    for (unsigned i = first; i <= last; ++i)
        if (!capture[i * 2] && !capture[i * 2 + 1]) ++quiet;
    // The sound the guest wrote is 4410 samples at 11.025 kHz, which is two
    // fifths of a second, or 17.640 frames at 44.1 kHz, and all of them have
    // to arrive. Previously, only about 9.450 of them did, because
    // DirectSound wrote a WAVEHDR that was still playing, got
    // WAVERR_STILLPLAYING and dropped what it had mixed into it.
    int peak = 0, crossings = 0;
    short previous = capture[first * 2];
    for (unsigned i = first; i <= last; ++i)
    {
        const short left = capture[i * 2];
        if (left > peak) peak = left;
        if ((previous < 0) != (left < 0)) ++crossings;
        previous = left;
    }
    // 100 of 128 full scale in eight bit is 25.600 in sixteen, and that is
    // exactly what arrives: DirectSound resamples 11.025 kHz to the primary
    // format and hands the result to waveOut without touching the amplitude.
    assert(peak > 24000 && peak < 26000);
    // 441 Hz over the stretch, two zero crossings per period.
    const int expected = 2 * 441 * (int)span / 44100;
    assert(crossings > expected - 8 && crossings < expected + 8);
    // 17.640 frames of sound; the count is first-to-last inclusive, so the very
    // last frame of the buffer is the zero the sine ends on and the span is one
    // short of the whole. A block boundary may cost one more frame at either
    // end, which is why this is a band of three and not a single number.
    assert(span >= 17637 && span <= 17641);
    // Silence inside the stretch is the zero crossings of the sine and nothing
    // else: 441 Hz over two fifths of a second is 352 of them.
    assert(quiet <= crossings + 4);
    bool found = false;
    printf("DirectSound guest PASS calls=%u frames=%u span=%u quiet=%u peak=%d crossings=%d"
           " loop_advances=%u loop_wraps=%u loop_highest=%u\n",
           app.ApiCalls(), frames, span, quiet, peak, crossings,
           observation("loop_advances", found), observation("loop_wraps", found),
           observation("loop_highest", found));
    printf("DirectSound play cursor travelled=%u expected=%u longest_stall=%u\n",
           observation("loop_travelled", found), observation("loop_expected", found),
           observation("loop_longest_stall", found));
    return 0;
}
