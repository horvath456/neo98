// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/dsound_capture_fixture.c over the original DSOUND.DLL of the
// tree on the test microphone of src/sound_microphone.cpp and checks what
// the original module answered and what the capture buffer held.
// DirectSoundCapture of Windows 98 has an emulation of its own: a card
// without a capture driver of DirectSound is recorded through waveIn, and
// DSOUND.DLL keeps the capture buffer filled from the WIM_DATA of its wave
// buffers. That is the path this port gives it (src/x86_wave_in.cpp), so
// every answer below comes from the original code.
//
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
static std::string debug_output;
static void collect(const char *text)
{
    debug_output += text;
    debug_output += '\n';
}
static unsigned observation(const char *name, bool &found)
{
    const std::string key = std::string("DSCAPTURE ") + name + "=";
    const auto at = debug_output.find(key);
    found = at != std::string::npos;
    if (!found) return 0;
    return (unsigned)strtoul(debug_output.c_str() + at + key.size(), nullptr, 16);
}
static unsigned failures = 0;
static unsigned expect(const char *name, unsigned want)
{
    bool found = false;
    const unsigned got = observation(name, found);
    if (!found || got != want)
    {
        fprintf(stderr, "DSCAPTURE %s=%08x, expected %08x%s\n", name, got, want, found ? "" : " (missing)");
        ++failures;
    }
    return got;
}
static unsigned within(const char *name, unsigned low, unsigned high)
{
    bool found = false;
    const unsigned got = observation(name, found);
    if (!found || got < low || got > high)
    {
        fprintf(stderr, "DSCAPTURE %s=%u, expected %u to %u%s\n", name, got, low, high, found ? "" : " (missing)");
        ++failures;
    }
    return got;
}
// The answers of one capture buffer. `bytes` is a second of the format.
static void buffer(const std::string &name, unsigned bytes, bool sine)
{
    const auto n = [&](const char *part) { static std::string key; key = name + part; return key.c_str(); };
    expect(n("_buffer"), 0);
    expect(n("_buffer_object"), 1);
    expect(n("_caps"), 0);
    expect(n("_caps_bytes"), bytes);
    expect(n("_start"), 0);
    // DSCBSTATUS_CAPTURING 1 and DSCBSTATUS_LOOPING 2.
    expect(n("_status"), 3);
    expect(n("_position"), 0);
    // The fixture waits until the read cursor is past the half second it
    // locks; the cursor is still inside the buffer.
    within(n("_read"), bytes / 2, bytes - 1);
    expect(n("_lock"), 0);
    expect(n("_lock_bytes"), bytes / 2);
    if (sine)
    {
        within(n("_peak"), 25000, 25600);
        // Half a second of 441 Hz.
        within(n("_crossings"), 438, 444);
    }
    else
    {
        expect(n("_peak"), 0);
        expect(n("_crossings"), 0);
    }
    expect(n("_mismatched"), 0);
    expect(n("_unlock"), 0);
    expect(n("_stop"), 0);
    expect(n("_status_end"), 0);
    expect(n("_release"), 0);
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    const char *kind = argv[3];
    const char *variable = getenv("NEO98_TEST_MICROPHONE");
    assert(variable && !strcmp(variable, kind));
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    static short capture[4410 * 2];
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    app.log = collect;
    const std::string command = std::string("DSCAPT.EXE ") + kind;
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "DSCAPT.EXE", command.c_str()));
    for (unsigned n = 0; n < 8000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (getenv("NEO98_DSCAPTURE_DUMP")) fputs(debug_output.c_str(), stderr);
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "DSCAPTURE status=%u code=%u error=%s\n%s\n", app.Status(), app.ExitCode(), app.Error(),
                debug_output.c_str());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    expect("loaded", 1);
    expect("found", 1);
    expect("enumerate", 0);
    if (!strcmp(kind, "none"))
    {
        // No wave input device. DSOUND still names its primary capture
        // driver, the entry without a GUID, but the create of the default
        // device fails with the error of the installed original DLL version.
        expect("devices", 1);
        expect("first_null", 1);
        expect("create", unsigned(strtoul(argv[4], nullptr, 0)));
        expect("object", 0);
    }
    else
    {
        // The primary capture driver (no GUID) and the device itself.
        expect("devices", 2);
        expect("first_null", 1);
        expect("create", 0);
        expect("object", 1);
        expect("getcaps", 0);
        // DSCCAPS_EMULDRIVER: the capture goes through waveIn.
        expect("caps_flags", 0x20);
        expect("caps_formats", 0xfff);
        expect("caps_channels", 2);
        const bool sine = !strcmp(kind, "sine");
        buffer("mono8", 11025, sine);
        buffer("stereo16", 44100 * 4, sine);
        expect("release", 0);
    }
    expect("done", 1);
    if (failures) fputs(debug_output.c_str(), stderr);
    assert(!failures);
    assert(!Sound::Microphone::OpenStreams());
    bool found = false;
    printf("DirectSoundCapture guest %s PASS calls=%u peak=%u crossings=%u received=%llu\n", kind, app.ApiCalls(),
           observation("stereo16_peak", found), observation("stereo16_crossings", found),
           Sound::Microphone::Received());
    return 0;
}
