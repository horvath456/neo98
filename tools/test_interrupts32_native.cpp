// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/interrupts32_fixture.c: software interrupts from 32-bit
// guest code through the native gate.
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
static void log(const char *p) { puts(p); }
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
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
    auto *a = new X86::Application(log);
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "INTR.EXE", ""));
    for (unsigned i = 0; i < 2000 && a->Status() != X86::Application::Failed &&
                         a->Status() != X86::Application::Exited;
         ++i)
        a->Pump(8);
    if (a->Status() != X86::Application::Exited || a->ExitCode())
    {
        fprintf(stderr, "FAIL status=%u code=%u %s\n", a->Status(), a->ExitCode(), a->Error());
        assert(false);
    }
    const unsigned calls = a->ApiCalls();
    delete a;
    printf("INTERRUPTS32 PASS calls=%u\n", calls);
}
