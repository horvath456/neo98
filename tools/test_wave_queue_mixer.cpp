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
    puts("PASS wave queue PCM, 2050 ordered completions, wrapped loop reuse");
}
