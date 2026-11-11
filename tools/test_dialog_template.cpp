// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/dialog_template.h"
#include "../src/pe32.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, size, f) == (unsigned)size);
    fclose(f);
    Pe32::Image image;
    assert(image.Parse(bytes.data(), bytes.size()));
    auto *t = new DialogResource::Template;
    unsigned truncated = 0;
    const unsigned counts[] = {74, 30, 7, 5};
    for (unsigned id = 101; id <= 104; ++id)
    {
        unsigned size = 0;
        const auto *data = image.Resource(5, id, &size);
        assert(data && t->Parse(data, size));
        assert(t->count == counts[id - 101]);
        unsigned consumed = t->consumed;
        assert(consumed <= size);
        if (id < 103)
        {
            assert(t->point == 8 && !strcmp(t->klass, "SciCalc") && t->menu == 106);
        }
        for (unsigned length = 0; length < consumed; ++length)
        {
            assert(!t->Parse(data, length));
            ++truncated;
        }
        assert(t->Parse(data, size));
    }
    // A control text longer than 255 characters, as in the German SQL
    // Server client configuration (CLICONFG.DLL dialog 103): the template
    // stays valid and the text comes out whole.
    {
        std::vector<unsigned char> d;
        auto word = [&](unsigned w) { d.push_back(w & 255); d.push_back(w >> 8); };
        auto dword = [&](unsigned v) { word(v & 65535); word(v >> 16); };
        dword(0x50000000); dword(0); word(1); word(0); word(0); word(100); word(50);
        word(0); word(0); word(0); // menu, class, title
        while (d.size() & 3) d.push_back(0);
        dword(0x50020000); dword(0); word(1); word(2); word(90); word(40); word(65535);
        word(65535); word(0x82);
        const unsigned length = 600;
        for (unsigned i = 0; i < length; ++i) word(i % 26 == 25 ? 0xe4 : 'a' + i % 26);
        word(0); word(0);
        assert(t->Parse(d.data(), d.size()));
        assert(t->count == 1 && strlen(t->items[0].title) == length);
        assert((unsigned char)t->items[0].title[25] == 0xe4 && t->items[0].title[26] == 'a');
    }
    unsigned seed = 0x989898;
    unsigned char junk[512];
    for (unsigned test = 0; test < 5000; ++test)
    {
        for (auto &b : junk)
        {
            seed = seed * 1664525 + 1013904223;
            b = seed >> 24;
        }
        t->Parse(junk, test % sizeof junk);
    }
    delete t;
    printf("DIALOG TEMPLATE PASS originals=4 truncated=%u long-text=1 malformed=5000\n", truncated);
}
