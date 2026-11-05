// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/export_contract_fixture.c and prints the counters it wrote.
// Argument three is the mode: "walk" for the ordinary run, "call-missing" for
// the run that calls an export this port does not implement, which must stop
// with the diagnosis instead of returning. tools/test_export_contract_guest.py
// compares.
#define private public
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
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

static constexpr unsigned Records = 0x30000000u, Words = 256;

int main(int argc, char **argv)
{
    assert(argc == 4);
    const bool call_missing = !strcmp(argv[3], "call-missing");
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    X86::Application app(nullptr);
    // COMDLG32 runs as the original under the DLL rule. The contract of its
    // native provider is measured here, so this one process takes it back.
    assert(app.AddNativeModule("COMDLG32.DLL"));
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "CONTRACT.EXE",
                     call_missing ? "CONTRACT.EXE call-missing" : nullptr));
    for (unsigned n = 0; n < 400000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
        app.Pump(32);
    std::vector<unsigned> words(Words);
    const bool read = app.Read(Records, words.data(), Words * 4);
    printf("CONTRACT mode=%s status=%u code=%u calls=%u read=%u\n", argv[3], app.Status(),
           app.ExitCode(), app.ApiCalls(), read ? 1 : 0);
    printf("CONTRACT error=%s\n", app.Error());
    if (read)
    {
        for (unsigned i = 0; i < 19; ++i)
            printf("COUNTER %u %u\n", i, words[i]);
        printf("FAILURES %u", words[32]);
        for (unsigned i = 0; i < words[32] && i < 64; ++i)
            printf(" %u", words[33 + i]);
        putchar('\n');
        printf("CHOSEN %u\n", words[128]);
    }
    return 0;
}
