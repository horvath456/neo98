// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/midi_fixture.c and checks the samples the synthesizer produced.
//
// The mixer has no host audio device here, so it runs on its own clock and
// writes every mixed frame into the capture buffer this runner hands it. The
// guest sounds note 69, which is 440 Hz, first over midiOutShortMsg and then
// over a MIDIEVENT stream; the check is that both stretches came out of the
// mixer at 44.1 kHz with 440 Hz in them.
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
static short capture[44100 * 8 * 2];

// One stretch of sound: where it starts, how long it is, its peak and how
// often it crosses zero. A stretch ends at `gap` silent frames.
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
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "MIDI.EXE"));
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "MIDI status=%u code=%u error=%s\n", app.Status(), app.ExitCode(),
                app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());

    const unsigned frames = Sound::Captured();
    assert(frames > 44100);
    // The first stretch is the note the guest sounded with midiOutShortMsg; it
    // is separated from the stream by half a second of silence.
    Stretch note;
    assert(Find(0, frames, 44100 / 5, note));
    assert(note.span > 44100 / 8);
    assert(note.peak > 1000);
    // 440 Hz crosses zero 880 times a second. The envelope only changes the
    // amplitude, not the period.
    int expected = 2 * 440 * (int)note.span / 44100;
    assert(note.crossings > expected - 6 && note.crossings < expected + 6);

    // The second stretch is the MIDIEVENT stream, one quarter note long.
    Stretch stream;
    assert(Find(note.first + note.span + 44100 / 5, frames, 44100 / 5, stream));
    assert(stream.span > 44100 / 8);
    assert(stream.peak > 1000);
    expected = 2 * 440 * (int)stream.span / 44100;
    assert(stream.crossings > expected - 6 && stream.crossings < expected + 6);

    printf("midi guest PASS calls=%u frames=%u note=%u/%d/%d stream=%u/%d/%d\n", app.ApiCalls(),
           frames, note.span, note.peak, note.crossings, stream.span, stream.peak,
           stream.crossings);
    return 0;
}
