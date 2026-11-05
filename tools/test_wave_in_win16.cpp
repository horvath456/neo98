// SPDX-License-Identifier: GPL-2.0-or-later
// Runs WAVEIN16.EXE of tools/wave_in_win16_fixture.asm, the MMSYSTEM waveIn
// entries of a 16-bit program, on the test microphone of
// src/sound_microphone.cpp. The program measures the second it recorded
// itself and ends with the number of the first check that failed; with the
// test device "none" it ends with 99 after waveInGetNumDevs. Argument three
// is the exit code this run expects.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    const unsigned expected = (unsigned)atoi(argv[3]);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    static short capture[4410 * 2];
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "WAVEIN16.EXE"));
    for (unsigned n = 0; n < 4000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    printf("WAVEIN16 status=%u code=%u error=%s received=%llu\n", app.Status(), app.ExitCode(), app.Error(),
           Sound::Microphone::Received());
    assert(app.Status() == X86::Application::Exited && app.ExitCode() == expected);
    assert(!Sound::Microphone::OpenStreams());
    return 0;
}
