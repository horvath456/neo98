// SPDX-License-Identifier: GPL-2.0-or-later
// growing queues preserve PCM, loop blocks and uncollected completions.
#include "../src/sound.h"
#include <cassert>
#include <cstdio>
#include <vector>
namespace Sound::Speaker { void Reset() {} }
namespace Sound::Microphone { void ResetAll() {} }
int main()
{
    auto &mixer = Sound::Shared();
    const unsigned source = mixer.Open({Sound::OutputRate, 1, 16});
    assert(source);
    const short pcm[4] = {8192, 8192, 8192, 8192};
    constexpr unsigned count = 1025;
    for (unsigned i = 1; i <= count; ++i)
        assert(!mixer.Submit(source, (const unsigned char *)pcm, sizeof pcm, i, 0, 0));
    std::vector<short> out((count * 4 + 1) * 2);
    mixer.Render(out.data(), count * 4 + 1);
    for (unsigned i = 0; i < count * 4 * 2; ++i) assert(out[i] == 8192);
    assert(!out[out.size()-1] && !out[out.size()-2]);
    // Completion storage must grow even when the playback queue is empty.
    for (unsigned i = 1; i <= count; ++i)
        assert(!mixer.Submit(source, (const unsigned char *)pcm, sizeof pcm, count+i, 0, 0));
    mixer.Reset(source);
    for (unsigned i = 1; i <= count*2; ++i) assert(mixer.Complete(source) == i);
    assert(!mixer.Complete(source));
    // Advance the sequence to 4070 in the now 4096-slot ring, so the loop
    // below really crosses its end. Empty headers consume no sample pool.
    for (unsigned i = 0; i < 2020; ++i)
        assert(!mixer.Submit(source, nullptr, 0, 10000 + i, 0, 0));
    mixer.Reset(source);
    for (unsigned i = 0; i < 2020; ++i) assert(mixer.Complete(source) == 10000 + i);
    assert(!mixer.Complete(source));
    // Retain all blocks across both loop passes and the wrapped ring indices.
    for (unsigned i = 1; i <= 129; ++i)
        assert(!mixer.Submit(source, (const unsigned char *)pcm, sizeof pcm, i,
                            i == 1 ? Sound::BeginLoop : i == 129 ? Sound::EndLoop : 0, 2));
    out.resize((129 * 4 * 2 + 4) * 2);
    mixer.Render(out.data(), 129 * 4 * 2 + 4);
    unsigned audible = 0;
    for (unsigned i = 0; i < out.size(); i += 2)
    {
        assert(out[i] == out[i+1]);
        assert(out[i] == 8192 || !out[i]);
        audible += out[i] != 0;
    }
    assert(audible == 129 * 4 * 2);
    for (unsigned i = 1; i <= 129; ++i) assert(mixer.Complete(source) == i);
    assert(!mixer.Complete(source) && !mixer.Busy(source));
    mixer.Close(source);
    // DOS produces 630-frame blocks while the device consumes 1024. A
    // bounded catch-up must retain enough old PCM to fill the next callback.
    const unsigned live = mixer.Open({Sound::OutputRate, 2, 16});
    assert(live);
    std::vector<short> samples(2520 * 2);
    for (unsigned i = 0; i < 2520; ++i) samples[i * 2] = samples[i * 2 + 1] = short(i + 1);
    for (unsigned i = 0; i < 4; ++i)
        assert(!mixer.SubmitLive(live, reinterpret_cast<const unsigned char *>(samples.data() + i * 1260),
                                 630 * 4, i + 1, 2205));
    assert(mixer.Played(live) == 315 * 4);
    out.resize(1024 * 2);
    mixer.Render(out.data(), 1024);
    for (unsigned i = 0; i < 1024; ++i)
        assert(out[i * 2] == short(316 + i) && out[i * 2 + 1] == out[i * 2]);
    assert(mixer.Complete(live) == 1 && mixer.Complete(live) == 2 && !mixer.Complete(live));
    // A producer burst larger than the limit discards complete old blocks
    // and only the excess prefix of the newest block. Tags stay ordered.
    assert(!mixer.SubmitLive(live, reinterpret_cast<const unsigned char *>(samples.data()),
                             unsigned(samples.size() * sizeof(short)), 5, 2205));
    assert(mixer.Complete(live) == 3 && mixer.Complete(live) == 4 && !mixer.Complete(live));
    out.resize(2206 * 2);
    mixer.Render(out.data(), 2206);
    for (unsigned i = 0; i < 2205; ++i)
        assert(out[i * 2] == short(316 + i) && out[i * 2 + 1] == out[i * 2]);
    assert(!out[4410] && !out[4411]);
    assert(mixer.Complete(live) == 5 && !mixer.Complete(live) && !mixer.Busy(live));
    mixer.Close(live);
    puts("PASS wave queue PCM, 2050 ordered completions, wrapped loop reuse");
    puts("PASS bounded live PCM retains the newest frames without a callback gap");
}
