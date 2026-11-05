// SPDX-License-Identifier: GPL-2.0-or-later
// Runner for tools/session_arena_fixture.c and tools/session_arena_shared.c:
// one guest program that starts itself as its own child, over a DLL with a
// section that carries IMAGE_SCN_MEM_SHARED and a heap from HeapCreate with
// HEAP_SHARED.
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
    assert(argc == 6);
    auto exe = load(argv[1]), dll = load(argv[2]), foreign = load(argv[3]);
    auto collision = load(argv[4]), font = load(argv[5]);
    auto &files = Files::System();
    assert(!files.Seed("C:\\ARENA.EXE", exe.data(), exe.size()));
    assert(!files.Seed("C:\\SHRD.DLL", dll.data(), dll.size()));
    assert(!files.Seed("C:\\FOREIGN.DLL", foreign.data(), foreign.size()));
    assert(!files.Seed("C:\\COLLIDE.DLL", collision.data(), collision.size()));
    auto *a = new X86::Application(log);
    assert(a->Start(exe.data(), exe.size(), font.data(), font.size(), "ARENA.EXE", "P"));
    for (unsigned i = 0; i < 6000 && a->Status() != X86::Application::Failed &&
                         a->Status() != X86::Application::Exited;
         ++i)
        a->Pump(8);
    if (a->Status() != X86::Application::Exited || a->ExitCode())
    {
        fprintf(stderr, "FAIL status=%u code=%u %s children=%u\n", a->Status(), a->ExitCode(), a->Error(),
                X86::Application::LiveChildren());
        assert(false);
    }
    const unsigned calls = a->ApiCalls();
    delete a;
    assert(!X86::Application::LiveChildren() && !X86::Application::ChildRecords());
    printf("SESSION ARENA PASS calls=%u\n", calls);
}
