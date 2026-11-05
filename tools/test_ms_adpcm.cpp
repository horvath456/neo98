// SPDX-License-Identifier: GPL-2.0-or-later
// Decodes the data chunk of a Microsoft ADPCM wave file to raw 16-bit samples.
#include "../src/ms_adpcm.h"
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    FILE *in = fopen(argv[1], "rb");
    if (!in) return 3;
    std::vector<unsigned char> bytes(8u << 20);
    const size_t length = fread(bytes.data(), 1, bytes.size(), in);
    fclose(in);
    unsigned at = 12, format = 0, format_bytes = 0, data = 0, data_bytes = 0;
    while (at + 8 <= length)
    {
        const unsigned chunk = bytes[at + 4] | bytes[at + 5] << 8 | bytes[at + 6] << 16 | (unsigned)bytes[at + 7] << 24;
        if (!memcmp(&bytes[at], "fmt ", 4)) { format = at + 8; format_bytes = chunk; }
        if (!memcmp(&bytes[at], "data", 4)) { data = at + 8; data_bytes = chunk; }
        at += 8 + chunk + (chunk & 1);
    }
    MsAdpcm::Layout layout;
    if (!format || !data || !MsAdpcm::ReadLayout(&bytes[format], format_bytes, layout)) return 4;
    std::vector<short> out(MsAdpcm::DecodedSamples(layout, data_bytes));
    MsAdpcm::Decode(layout, &bytes[data], data_bytes, out.data());
    FILE *result = fopen(argv[2], "wb");
    if (!result) return 5;
    fwrite(out.data(), sizeof(short), out.size(), result);
    fclose(result);
    return 0;
}
