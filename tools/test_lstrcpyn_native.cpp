// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb"); assert(f);
    fseek(f, 0, SEEK_END); const long size = ftell(f); rewind(f);
    std::vector<unsigned char> bytes(size);
    assert(fread(bytes.data(), 1, size, f) == size_t(size)); fclose(f);
    return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    auto exe = load(argv[1]), font = load(argv[2]);
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "COPYTEST.EXE"));
    // The original wrapper catches invalid pointers, even for count == 0.
    const unsigned destination = app.Allocate(32);
    for (unsigned count : {0u, 1u, 20u}) {
        const unsigned args[] = {destination, 0, count};
        app.last_error = 123;
        assert(app.Api("lstrcpynA", args) == 0 && app.last_error == 87);
        assert(app.Status() != X86::Application::Failed);
    }
    FILE *corpus = fopen(argv[3], "rb"); assert(corpus);
    unsigned cases = 0; assert(fread(&cases, 4, 1, corpus) == 1);
    constexpr unsigned base = 0x41000000;
    assert(app.Map(base, 8192, 3));
    for (unsigned c = 0; c < cases; ++c)
    {
        unsigned config[7], expected[3];
        unsigned char before[8192], after[8192], actual[8192];
        assert(fread(config, sizeof config, 1, corpus) == 1);
        assert(fread(expected, sizeof expected, 1, corpus) == 1);
        assert(fread(before, sizeof before, 1, corpus) == 1);
        assert(fread(after, sizeof after, 1, corpus) == 1);
        assert(app.Store(base, before, sizeof before));
        for (unsigned page = 0; page < 2; ++page)
        {
            const unsigned access = !(config[4] & (1u << page)) ? 1 :
                                    config[5] & (1u << page) ? 4 : 2;
            const unsigned args[] = {base + page * 4096, 4096, access, 0};
            assert(app.MemoryApi("VirtualProtect", args));
        }
        app.last_error = config[6];
        const unsigned args[] = {config[1], config[2]};
        const unsigned result = app.Api("lstrcpyA", args);
        assert(result == expected[0] && app.last_error == expected[1]);
        assert(app.Status() != X86::Application::Failed);
        const unsigned restore[] = {base, 8192, 4, 0};
        assert(app.MemoryApi("VirtualProtect", restore));
        assert(app.Read(base, actual, sizeof actual));
        if (memcmp(actual, after, sizeof actual))
            for (unsigned at = 0; at < sizeof actual; ++at)
                if (actual[at] != after[at])
                {
                    fprintf(stderr, "copy case %u offset %u actual=%02x expected=%02x\n", c, at, actual[at], after[at]);
                    break;
                }
        assert(!memcmp(actual, after, sizeof actual));
    }
    fclose(corpus);
    for (unsigned i = 0; i < 1000 && app.Status() != X86::Application::Exited &&
         app.Status() != X86::Application::Failed; ++i) app.Pump(16);
    printf("LSTRCPYN state=%u exit=%u error=%s\n", app.Status(), app.ExitCode(), app.Error());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
}
