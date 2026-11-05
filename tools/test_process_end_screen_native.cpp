// SPDX-License-Identifier: GPL-2.0-or-later
// a child takes the screen (640x480x8), sets and hides its pointer and
// ends without cleanup. Win98 gives the display mode back when the owner dies
// and forgets the ShowCursor level with its queue; the pointer
// stays a shared OEM cursor and is drawn again over the parent's window. The
// port kept the shape in the icon table of the child, which is freed with it.
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

// Pixels in the box around the hotspot that are not the parent's white.
static unsigned PointerPixels(int x, int y)
{
    const unsigned char *frame = User::Pixels();
    assert(frame);
    unsigned n = 0;
    for (int row = y - 16; row < y + 16; ++row)
        for (int column = x - 16; column < x + 16; ++column)
        {
            const unsigned char *p = frame + (row * 800 + column) * 4;
            if (p[0] != 0xff || p[1] != 0xff || p[2] != 0xff) ++n;
        }
    return n;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    assert(!Files::System().Seed("C:\\ENDSCREEN.EXE", exe.data(), exe.size()));
    auto *app = new X86::Application(nullptr);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "ENDSCREEN.EXE", "PARENT"));
    int record[9] = {};
    for (unsigned n = 0; n < 20000 && record[1] != 1; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
        app->Read(Records, record, sizeof record);
    }
    assert(record[1] == 1);
    User::ShowPointer(true);
    app->MouseButtons(400, 300, 0);
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    const unsigned before = PointerPixels(400, 300);
    printf("END SCREEN before owner=%u pixels=%u\n", User::PointerOwner() == app ? 1u : 0u, before);
    assert(User::PointerOwner() == app && before > 0);
    const int go = 1;
    assert(app->Store(Records + 8, &go, 4));
    for (unsigned n = 0; n < 40000 && record[1] != 2; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
        app->Read(Records, record, sizeof record);
    }
    if (record[1] != 2)
        fprintf(stderr, "END SCREEN stage=%d status=%u exit=%u error=%s live=%u failed=%s calls=%u\n", record[1], unsigned(app->Status()),
                app->ExitCode(), app->Error(), X86::Application::LiveChildren(), X86::Application::FailedChild(0) ? X86::Application::FailedChild(0) : "-", app->ApiCalls());
    assert(record[1] == 2);
    // The pointer does not move: nothing sends WM_SETCURSOR to the parent.
    for (unsigned n = 0; n < 50; ++n) app->Pump(8);
    int width = 0, height = 0, depth = 0;
    User::ScreenMode(width, height, depth);
    const unsigned after = PointerPixels(400, 300);
    printf("END SCREEN after child=%08x mode=%dx%dx%d level=%d visible=%u owner=%u pixels=%u\n",
           unsigned(record[3]), width, height, depth, User::PointerCount(),
           User::PointerVisible() ? 1u : 0u, User::PointerOwner() == app ? 1u : 0u, after);
    // 7705: the child did not see the taskbar's inset in 640x480.
    printf("END SCREEN work area after child=%d,%d,%d,%d\n", record[5], record[6], record[7], record[8]);
    assert(unsigned(record[3]) == 0xffffffffu);
    assert(record[5] == 0 && record[6] == 0 && record[7] == 800 && record[8] == 572);
    assert(!User::ScreenModeActive() && width == 800 && height == 600 && depth == 32);
    assert(User::PointerCount() == 0 && User::PointerVisible());
    // Previously, the shape stayed with the freed child: no pointer, and
    // ASan reported the use of the freed Application in DrawCursorShape.
    assert(User::PointerOwner() == app && after > 0);
    const int end = 1;
    assert(app->Store(Records + 16, &end, 4));
    for (unsigned n = 0; n < 20000 && app->Status() != X86::Application::Exited; ++n) {
        app->Pump(8);
        assert(app->Status() != X86::Application::Failed);
    }
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0);
    // USER (SignalProc32 ): the windows of an ending
    // process go at once, not when the process is taken down.
    printf("END SCREEN windows after exit=%u\n", User::LiveWindows());
    assert(User::LiveWindows() == 0);
    delete app;
    printf("END SCREEN PASS\n");
    return 0;
}
