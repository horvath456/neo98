// SPDX-License-Identifier: GPL-2.0-or-later
// The session sets the working directory through the runtime's file session.
#define private public
#include "../src/x86_app.h"
#undef private
#include "linux_session.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <vector>
static unsigned draws = 0;
int main(int argc, char **argv)
{
    LinuxHost::Options options;
    options.exe = "C:\\WINDOWS\\NOTEPAD.EXE";
    LinuxHost::Session session(argc, argv, options);
    assert(session.Start());
    for (unsigned i = 0; i < 100; ++i)
    {
        session.app->Pump(8);
        User::Render();
        if (User::FindWindow("Notepad", nullptr) && !User::Rendering())
            break;
    }
    assert(User::FindWindow("Notepad", nullptr));
    assert(!User::Rendering());
    User::ShowPointer(false);
    User::Render();
    int width = 0, height = 0;
    User::PixelsSize(width, height);
    const std::vector<unsigned char> before(User::Pixels(), User::Pixels() + size_t(width) * height * 4);
    // DrawIcon succeeds even when clipping hides all of its pixels. The
    // next move must restore an empty region without a negative copy size.
    User::SetCursorProvider(nullptr, [](void *, Gdi::HDC, int, int, unsigned) {
        ++draws;
        return true;
    });
    User::SetPointerShape(1);
    User::ShowPointer(true);
    const int positions[][2] = {{width + 100, 100},  {-100, 100}, {INT_MAX, 100}, {INT_MIN, 100},
                                {100, height + 100}, {100, -100}, {100, INT_MAX}, {100, INT_MIN}};
    for (const auto &position : positions)
    {
        User::CaptureMouse(position[0], position[1], 0, 0);
        User::CaptureMouse(100, 100, 0, 0);
    }
    User::ShowPointer(false);
    User::SetCursorProvider(nullptr, nullptr);
    assert(draws >= 8);
    assert(std::vector<unsigned char>(User::Pixels(), User::Pixels() + before.size()) == before);
    printf("CURSOR BOUNDS PASS: off-screen and extreme coordinates preserve the frame\n");
}
