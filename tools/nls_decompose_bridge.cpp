// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_nls_decompose.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U = unsigned;
constexpr U Base = 0x160000, Bytes = 8192;
Win98NlsDecompose::UnicodeTable unicode;
// Codepage mapping is an explicit provider, with the same original file offsets
// as Win98Nls::Table; the bridge does not link mutable production wiring.
struct Codepage
{
    const unsigned char *data;
    U Half(U at) const { return data[at] | U(data[at + 1]) << 8; }
    U Offset(U n) const { return (Half(n * 4) | Half(n * 4 + 2) << 16) * 2; }
    U Wide(U byte, bool glyphs) const
    {
        return Half((glyphs && Offset(3) ? Offset(3) : Offset(2)) + byte * 2);
    }
};
struct Memory
{
    unsigned char *bytes;
    U error = 0x9898;
    bool Read(U at, void *out, U n)
    {
        assert(at >= Base && at - Base <= Bytes && n <= Bytes - (at - Base));
        std::memcpy(out, bytes + at - Base, n); return true;
    }
    bool Write(U at, const void *data, U n)
    {
        assert(at >= Base && at - Base <= Bytes && n <= Bytes - (at - Base));
        std::memcpy(bytes + at - Base, data, n); return true;
    }
    void Error(U value) { error = value; }
};
extern "C" U decompose_load(const unsigned char *bytes, U size) { return unicode.Load(bytes, size); }
extern "C" U decompose_pair(U character, U *pair) { return unicode.Pair(character, pair[0], pair[1]); }
extern "C" U decompose_expand(U *words, U capacity)
{
    struct Buffer
    {
        U *words;
        U Read16(U at) { return words[at / 2]; }
        void Write16(U at, U value) { words[at / 2] = value; }
    } buffer{words};
    return Win98NlsDecompose::Expand(buffer, unicode, 0, capacity * 2);
}
extern "C" void decompose_call(const unsigned char *cp, const U *a, unsigned char *bytes, U *out)
{
    assert(unicode.Valid());
    Memory e{bytes};
    U count = a[3];
    if (count & 0x80000000)
    {
        count = 0;
        unsigned char ch;
        do { e.Read(a[2] + count++, &ch, 1); } while (ch);
    }
    out[0] = Win98NlsDecompose::Convert(e, Codepage{cp}, unicode, a[1], a[2], count, a[4], a[5]);
    out[1] = e.error;
}
#ifdef NLS_DECOMPOSE_MAIN
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = std::fopen(path, "rb"); assert(f);
    std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); assert(n >= 0); std::rewind(f);
    std::vector<unsigned char> result(n);
    assert(std::fread(result.data(), 1, result.size(), f) == result.size()); std::fclose(f);
    return result;
}
int main(int argc, char **argv)
{
    assert(argc == 6);
    auto uni = load(argv[1]); assert(decompose_load(uni.data(), uni.size()));
    std::vector<unsigned char> cp[3] = {load(argv[2]), load(argv[3]), load(argv[4])};
    // Exercise the table and every bounded expansion under the sanitizers too.
    for (U ch = 0; ch < 65536; ++ch)
    {
        U pair[2] = {}; decompose_pair(ch, pair);
        for (U cap = 1; cap <= 5; ++cap)
        {
            U words[5] = {ch, 0xcccc, 0xcccc, 0xcccc, 0xcccc};
            const U length = decompose_expand(words, cap); assert(length && length <= cap);
        }
    }
    FILE *f = std::fopen(argv[5], "rb"); assert(f);
    U c[7], expected[2], actual[2], count = 0;
    unsigned char before[Bytes], after[Bytes];
    while (std::fread(c, sizeof c, 1, f) == 1)
    {
        assert(c[0] < 3 && std::fread(before, sizeof before, 1, f) == 1);
        assert(std::fread(expected, sizeof expected, 1, f) == 1);
        assert(std::fread(after, sizeof after, 1, f) == 1);
        decompose_call(cp[c[0]].data(), c + 1, before, actual);
        assert(!std::memcmp(actual, expected, sizeof actual));
        assert(!std::memcmp(before, after, sizeof before)); ++count;
    }
    assert(std::feof(f)); std::fclose(f);
    std::printf("NLS DECOMPOSE ASAN PASS %u conversion cases, all 65536 table entries and expansions\n", count);
}
#endif
