// SPDX-License-Identifier: GPL-2.0-or-later
// A stalled consumer must get recent DOS sound, not the oldest buffered tone.
#include "host_dos_vm.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char **argv)
{
    assert(argc == 2);
    HostDosVm::Drive drive{'D', argv[1], false};
    HostDosVm::Install(nullptr, &drive, 1);
    const auto *p = DosVm::Current();
    DosVm::Launch launch;
    snprintf(launch.directory, sizeof launch.directory, "D:\\");
    snprintf(launch.command, sizeof launch.command, "TONE.COM");
    char error[256]{};
    auto *session = p->Start(p->owner, launch, error, sizeof error);
    assert(session);
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    // The Windows consumer has not read anything. Turn the tone off in DOS,
    // but keep DOS running, so the latest sound is now silence.
    p->Key(p->owner, session, 13, 0x1c, false, true);
    p->Key(p->owner, session, 13, 0x1c, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    HostDosVm::Pause(true);
    const unsigned queued = HostDosVm::AudioQueued();
    std::vector<short> samples(2 * DosVm::AudioRate);
    const unsigned got = p->Audio(p->owner, session, samples.data(), DosVm::AudioRate);
    int peak = 0, low = 32767, high = -32768;
    for (unsigned i = 0; i < got * 2; ++i)
    {
        if (samples[i] < low) low = samples[i];
        if (samples[i] > high) high = samples[i];
        if (std::abs((int)samples[i]) > peak) peak = std::abs((int)samples[i]);
    }
    printf("DOS AUDIO stalled consumer queued=%u frames=%.1f ms peak=%d span=%d\n", queued,
           queued * 1000.0 / DosVm::AudioRate, peak, high-low);
    fflush(stdout);
    assert(got > 0 && got == queued && queued <= DosVm::AudioQueueFrames);
    assert(high - low < 16); // No old 1 kHz tone; the disabled speaker can retain DC.
    p->Stop(p->owner, session);
    HostDosVm::Pause(false);
    HostDosVm::Uninstall();
    puts("DOS AUDIO LATENCY PASS");
}
