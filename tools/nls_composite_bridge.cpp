// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_nls.h"
#include "../src/win98_nls_composite.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U = unsigned;
constexpr U Base = 0x160000, Bytes = 8192;
Win98NlsComposite::UnicodeTable unicode;
struct Memory
{
    unsigned char *bytes;
    U error = 0x9898;
    bool Read(U address, void *out, U n)
    {
        assert(address >= Base && address - Base <= Bytes && n <= Bytes - (address - Base));
        memcpy(out, bytes + address - Base, n);
        return true;
    }
    bool Write(U address, const void *in, U n)
    {
        assert(address >= Base && address - Base <= Bytes && n <= Bytes - (address - Base));
        memcpy(bytes + address - Base, in, n);
        return true;
    }
    void Error(U value) { error = value; }
};
extern "C" U composite_load(const unsigned char *bytes, U size) { return unicode.Load(bytes, size); }
extern "C" U composite_lookup(U base, U mark) { return unicode.Compose(base, mark); }
extern "C" U composite_nonspacing(U ch) { return unicode.NonSpacing(ch); }
extern "C" void composite_call(const unsigned char *cp, U size, const U *a, unsigned char *bytes, U *out)
{
    Win98Nls::Table table{cp, size};
    assert(table.Valid() && unicode.Valid());
    Memory memory{bytes};
    U count = a[3];
    if (count & 0x80000000)
    {
        count = 0;
        unsigned short ch;
        do
        {
            memory.Read(a[2] + count * 2, &ch, 2);
            ++count;
        } while (ch);
    }
    out[0] = Win98NlsComposite::Convert(memory, table, unicode, a[1], a[2], count, a[4], a[5], a[6], a[7]);
    out[1] = memory.error;
}
#ifdef NLS_COMPOSITE_MAIN
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    const long length = ftell(f);
    assert(length >= 0);
    rewind(f);
    std::vector<unsigned char> data(length);
    assert(fread(data.data(), 1, data.size(), f) == data.size());
    fclose(f);
    return data;
}
int main(int argc, char **argv)
{
    assert(argc == 6);
    const auto data = load(argv[1]);
    assert(composite_load(data.data(), data.size()));
    std::vector<unsigned char> cp[3] = {load(argv[2]), load(argv[3]), load(argv[4])};
    FILE *cases = fopen(argv[5], "rb");
    assert(cases);
    U config[9], expected[2], actual[2], count = 0;
    unsigned char memory[Bytes], after[Bytes];
    while (fread(config, sizeof config, 1, cases) == 1)
    {
        assert(config[0] < 3 && fread(memory, sizeof memory, 1, cases) == 1);
        assert(fread(expected, sizeof expected, 1, cases) == 1 && fread(after, sizeof after, 1, cases) == 1);
        composite_call(cp[config[0]].data(), cp[config[0]].size(), config + 1, memory, actual);
        assert(!memcmp(expected, actual, sizeof expected) && !memcmp(after, memory, sizeof memory));
        ++count;
    }
    fclose(cases);
    printf("NLS COMPOSITE ASAN PASS %u cases\n", count);
}
#endif
