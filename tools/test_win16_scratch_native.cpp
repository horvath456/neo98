// SPDX-License-Identifier: GPL-2.0-or-later
// The staging blocks of the Win16 gates, one per nesting depth. A
// gate whose shared body calls back into the task can meet a second gate
// there; that one must get a block of its own and leave the first one alone.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cstdio>
#include <cstdlib>
#include <vector>
static void require(bool ok, const char *why)
{
    if (!ok) { fprintf(stderr, "WIN16 SCRATCH FAILED: %s\n", why); exit(1); }
}
static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    require(f, "input");
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    require(fread(b.data(), 1, n, f) == size_t(n), "read input");
    fclose(f);
    return b;
}
int main(int argc, char **argv)
{
    require(argc == 3, "usage: runner SCRATCH.EXE fixedsys.fnt");
    auto exe = load(argv[1]), font = load(argv[2]);
    X86::Application app(nullptr);
    require(app.Start(exe.data(), exe.size(), font.data(), font.size(), "SCRATCH.EXE"), "start");
    for (unsigned i = 0; i < 2000 && !app.win16.state && app.Status() != X86::Application::Failed; ++i)
        app.Pump(1);
    require(app.win16.state != nullptr, "the task is a Win16 task");
    const unsigned saved = app.win16_gate_depth;
    app.win16_gate_depth = 1;
    const unsigned outer = app.Win16Scratch();
    require(outer != 0, "a block for the outer gate");
    app.Write32(outer, 0x13551355u);
    app.Write32(outer + 4092, 0x5a5a5a5au);
    // The body of the outer gate calls back, and a gate runs in the callback.
    app.win16_gate_depth = 2;
    const unsigned inner = app.Win16Scratch();
    require(inner != 0 && inner != outer, "the inner gate gets a block of its own");
    app.Write32(inner, 0xdeadbeefu);
    require(app.Read32(outer) == 0x13551355u && app.Read32(outer + 4092) == 0x5a5a5a5au,
            "the staging of the outer gate survives the inner one");
    // Back in the outer gate: the next gate at that depth starts from zero.
    app.win16_gate_depth = 1;
    require(app.Win16Scratch() == outer && app.Read32(outer) == 0, "the same depth reuses its block");
    app.win16_gate_depth = saved;
    printf("WIN16 SCRATCH PASS outer=%08x inner=%08x\n", outer, inner);
    return 0;
}
