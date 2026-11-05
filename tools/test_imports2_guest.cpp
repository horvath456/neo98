// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/imports2_fixture.c and prints the counters it wrote.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
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
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application app(nullptr);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "IMPORTS2.EXE", nullptr));
    for (unsigned n = 0; n < 400000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
        app.Pump(32);
    unsigned words[8] = {};
    const bool read = app.Read(0x30000000u, words, sizeof words);
    printf("IMPORTS2 status=%u code=%u calls=%u read=%u\n", app.Status(), app.ExitCode(),
           app.ApiCalls(), read ? 1 : 0);
    printf("IMPORTS2 error=%s\n", app.Error());
    printf("IMPORTS2 checks=%u bad=%u first=%u\n", words[0], words[1], words[2]);
    return 0;
}
