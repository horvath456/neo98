// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Microsoft ADPCM (WAVE_FORMAT_ADPCM, tag 2) to 16-bit PCM.
//
// Win98 plays a compressed wave through the wave mapper, which asks the ACM
// for a converter; the codec for this format is MSADP32.ACM. The sounds of
// the installed scheme use it (C:\WINDOWS\MEDIA\START.WAV, the Explorer
// "Navigating" event), so PlaySound has to read it. The algorithm is the
// published one: every block starts, per channel, with a predictor index, a
// step size and two samples; each following nibble is a signed correction to
// a linear prediction from the two previous samples, and the step adapts
// through a fixed table.
#include <stddef.h>
#include <stdint.h>

namespace MsAdpcm
{
struct Layout
{
    unsigned channels = 0, block_align = 0, samples_per_block = 0, coefficients = 0;
    short coefficient[32][2] = {};
};

// Reads the format extension behind the 16 bytes of a PCM header: cbSize,
// wSamplesPerBlock, wNumCoef and the coefficient pairs.
inline bool ReadLayout(const unsigned char *format, unsigned bytes, Layout &out)
{
    const auto word = [&](unsigned at) { return (unsigned)format[at] | ((unsigned)format[at + 1] << 8); };
    if (bytes < 22) return false;
    out.channels = word(2);
    out.block_align = word(12);
    out.samples_per_block = word(18);
    out.coefficients = word(20);
    if (!out.channels || out.channels > 2 || out.coefficients > 32 ||
        bytes < 22 + out.coefficients * 4 || out.block_align < 7 * out.channels ||
        out.samples_per_block < 2)
        return false;
    for (unsigned i = 0; i < out.coefficients; ++i)
    {
        out.coefficient[i][0] = (short)word(22 + i * 4);
        out.coefficient[i][1] = (short)word(24 + i * 4);
    }
    return true;
}

// The number of 16-bit samples, all channels counted, that `bytes` of data
// decode to. A short last block carries as many samples as its nibbles give.
inline size_t DecodedSamples(const Layout &layout, size_t bytes)
{
    size_t total = 0;
    while (bytes >= 7 * layout.channels)
    {
        const size_t block = bytes < layout.block_align ? bytes : layout.block_align;
        size_t frames = 2 + (block - 7 * layout.channels) * 2 / layout.channels;
        if (frames > layout.samples_per_block) frames = layout.samples_per_block;
        total += frames * layout.channels;
        bytes -= block;
    }
    return total;
}

// Decodes into `out`, which holds DecodedSamples entries, interleaved.
inline void Decode(const Layout &layout, const unsigned char *data, size_t bytes, short *out)
{
    static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614,
                                  768, 614, 512, 409, 307, 230, 230, 230};
    const unsigned channels = layout.channels;
    while (bytes >= 7 * channels)
    {
        const size_t block = bytes < layout.block_align ? bytes : layout.block_align;
        int first[2] = {}, second[2] = {}, delta[2] = {}, one[2] = {}, two[2] = {};
        const unsigned char *at = data;
        for (unsigned c = 0; c < channels; ++c)
        {
            const unsigned index = *at++;
            first[c] = index < layout.coefficients ? layout.coefficient[index][0] : 0;
            second[c] = index < layout.coefficients ? layout.coefficient[index][1] : 0;
        }
        for (unsigned c = 0; c < channels; ++c, at += 2) delta[c] = (short)(at[0] | at[1] << 8);
        for (unsigned c = 0; c < channels; ++c, at += 2) one[c] = (short)(at[0] | at[1] << 8);
        for (unsigned c = 0; c < channels; ++c, at += 2) two[c] = (short)(at[0] | at[1] << 8);
        size_t frames = 2 + (block - 7 * channels) * 2 / channels;
        if (frames > layout.samples_per_block) frames = layout.samples_per_block;
        for (unsigned c = 0; c < channels; ++c) *out++ = (short)two[c];
        for (unsigned c = 0; c < channels; ++c) *out++ = (short)one[c];
        bool high = true;
        for (size_t frame = 2; frame < frames; ++frame)
            for (unsigned c = 0; c < channels; ++c)
            {
                const unsigned nibble = high ? *at >> 4 : *at++ & 15;
                high = !high;
                const int correction = nibble & 8 ? (int)nibble - 16 : (int)nibble;
                int sample = (one[c] * first[c] + two[c] * second[c]) / 256 + correction * delta[c];
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                *out++ = (short)sample;
                two[c] = one[c];
                one[c] = sample;
                delta[c] = adapt[nibble] * delta[c] / 256;
                if (delta[c] < 16) delta[c] = 16;
                // An encoder never gets here; bytes that are not a real stream
                // must not run the step size out of an int.
                if (delta[c] > 0x7fffffff / 768) delta[c] = 0x7fffffff / 768;
            }
        data += block;
        bytes -= block;
    }
}
} // namespace MsAdpcm
