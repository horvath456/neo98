// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/wave_in_fixture.c on the test microphone of
// src/sound_microphone.cpp. The kind of the test device comes from
// NEO98_TEST_MICROPHONE, which tools/test_wave_in_guest.py sets, and goes to
// the guest as the last word of its command line.
//
// The guest measures the recorded samples itself; the runner checks what the
// device saw: how many frames came in, that every stream is closed again and
// that the device is off once the program has ended, and the state the host
// side reported (denied stays denied, silence and sine run).
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
int main(int argc, char **argv)
{
    assert(argc == 4);
    const char *kind = argv[3];
    const char *variable = getenv("NEO98_TEST_MICROPHONE");
    assert(variable && !strcmp(variable, kind));
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    // No output provider: the mixer keeps its own clock and plays nothing.
    static short capture[4410 * 2];
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    std::vector<std::string> lines;
    static std::vector<std::string> *sink = &lines;
    app.log = [](const char *text) { sink->push_back(text); };
    const std::string command = std::string("WAVEIN.EXE ") + kind;
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "WAVEIN.EXE", command.c_str()));
    unsigned max_state = 0;
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (Sound::Microphone::State() > max_state) max_state = Sound::Microphone::State();
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "WAVEIN %s status=%u code=%u error=%s\n", kind, app.Status(), app.ExitCode(),
                app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    assert(!Sound::Microphone::OpenStreams());
    assert(Sound::Microphone::State() == Sound::InputOff);
    const unsigned long long received = Sound::Microphone::Received();
    if (!strcmp(kind, "none"))
    {
        assert(!received && max_state == Sound::InputOff);
    }
    else
    {
        // Eight recordings of half a second, the Stop test and three more
        // buffers: well over four seconds of frames went through the ring.
        assert(received > 44100ull * 4);
        assert(max_state == (!strcmp(kind, "denied") ? Sound::InputDenied : Sound::InputRunning));
        bool passed = false;
        for (const auto &line : lines)
        {
            passed |= line.find("WAVEIN PASS") != std::string::npos;
            if (line.find("OutputDebugString WAVEIN ") == 0 && line.find("PASS") == std::string::npos)
                printf("%s %s\n", kind, line.c_str() + 18);
        }
        assert(passed);
    }
    printf("waveIn guest %s PASS calls=%u received=%llu state=%u\n", kind, app.ApiCalls(), received, max_state);
    return 0;
}
