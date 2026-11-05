// SPDX-License-Identifier: GPL-2.0-or-later
// a program hides the pointer with ShowCursor(FALSE) and ends without
// showing it again. USER keeps the level in the message queue (
// [queue+3Ah]) and draws the pointer according to the cursor queue,
// which belongs to the window under the pointer. The
// queue dies with the program, so the pointer is back afterwards.
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

static constexpr unsigned Records = 0x30000000u;

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "HIDE.EXE"));
    int record[2] = {};
    for (unsigned n = 0; n < 20000 && record[1] != 1; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
        app->Read(Records, record, sizeof record);
    }
    assert(record[1] == 1);
    app->MouseButtons(400, 300, 0);
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    const int hidden_level = User::PointerCount();
    const bool hidden_visible = User::PointerVisible();
    printf("CURSOR HIDE answer=%d level=%d visible=%u shape=%u\n", record[0], hidden_level,
           hidden_visible ? 1u : 0u, User::PointerShape());
    assert(record[0] == -1);
    assert(hidden_level == -1 && !hidden_visible);
    const int go = 1;
    assert(app->Store(Records + 8, &go, 4));
    for (unsigned n = 0; n < 20000 && app->Status() != X86::Application::Exited; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
    }
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    const int level = User::PointerCount();
    printf("CURSOR HIDE after exit level=%d visible=%u shape=%u\n", level, User::PointerVisible() ? 1u : 0u,
           User::PointerShape());
    // Previously, the level was one word for the whole system and stayed at
    // -1 here, so the pointer never came back.
    assert(level == 0);
    delete app;
    printf("CURSOR HIDE PASS\n");
    return 0;
}
