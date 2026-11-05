// SPDX-License-Identifier: GPL-2.0-or-later
// Drives the x86 fixture of tools/frame_track_fixture.c: the hit-test matrix and
// the SCROLLBAR control run inside the guest, while this runner supplies the
// pointer input for the move, size and scrollbar trackers and checks the
// resulting geometry and pixels.
#include "../src/x86_app.h"
#include "../src/user_settings.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<unsigned char> Load(const char *name)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> v(n);
    assert(fread(v.data(), 1, n, f) == size_t(n));
    fclose(f);
    return v;
}
namespace {
X86::Application *app = nullptr;
using Clock = std::chrono::steady_clock;
// Real time has to pass for the repeat timer, so the pump runs against the wall
// clock rather than a slice count.
void Pump(unsigned milliseconds)
{
    const auto until = Clock::now() + std::chrono::milliseconds(milliseconds);
    do { app->Pump(4); } while (Clock::now() < until);
}
bool Alive() { return app->Status() != X86::Application::Exited && app->Status() != X86::Application::Failed; }
unsigned Track()
{
    for (unsigned i = 0; i < 4000 && Alive(); ++i)
    {
        if (const unsigned h = User::FindWindow("TrackPort", "tracking")) return h;
        app->Pump(8);
    }
    return 0;
}
// The fixture checks its own recorded trace and exits with the failing code;
// a phase that ended the process is a failed message-order check.
void Phase(unsigned window, unsigned number)
{
    assert(User::PostMessage(window, 0x8000, number, 0));
    Pump(120);
    if (!Alive())
        fprintf(stderr, "FRAME phase %u failed: status=%u code=%u\n", number,
                app->Status(), app->ExitCode());
    assert(Alive());
}
void Press(int x, int y) { app->MouseButtons(x, y, 1); }
void Move(int x, int y) { app->MouseButtons(x, y, 1); }
void Release(int x, int y) { app->MouseButtons(x, y, 0); }
std::vector<unsigned char> Frame()
{
    assert(User::Render() || User::Pixels());
    return std::vector<unsigned char>(User::Pixels(), User::Pixels() + 800 * 600 * 4);
}
unsigned Differences(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
                     int left, int top, int right, int bottom)
{
    unsigned n = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            if (memcmp(a.data() + (y * 800 + x) * 4, b.data() + (y * 800 + x) * 4, 4)) ++n;
    return n;
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);
    auto exe = Load(argv[1]), font = Load(argv[2]);
    const unsigned before_objects = Gdi::LiveObjects();
    {
        X86::Application application(nullptr);
        app = &application;
        assert(application.Start(exe.data(), exe.size(), font.data(), font.size(), "FRAME.EXE"));
        const unsigned window = Track();
        if (!window)
            fprintf(stderr, "FRAME guest did not reach its loop: status=%u code=%u error=%s\n",
                    application.Status(), application.ExitCode(), application.Error());
        assert(window);
        Gdi::RECT rect{};
        assert(User::GetWindowRect(window, &rect));
        const int width = rect.right - rect.left, height = rect.bottom - rect.top;
        assert(width == 400 && height == 300);

        // A size drag of the bottom right corner. The drag frame follows the
        // pointer, the window itself only moves on release.
        auto screen_pixel = [](int x, int y) {
            unsigned value = 0;
            memcpy(&value, User::ScreenBits() + y * User::ScreenStride() + x * 4, 4);
            return value & 0xffffff;
        };
        // A work area above a 40 pixel strip, as a taskbar leaves it.
        User::SystemSettings().work[3] = 560;
        Frame();
        const unsigned corner = screen_pixel(rect.left, rect.top), beside = screen_pixel(rect.left + 1, rect.top);
        Press(rect.right - 2, rect.bottom - 2);
        Pump(60);
        // USER: ClipCursor on the drag rectangle; for a top-level
        // window that is neither topmost nor a tool window the work area

        {
            Gdi::RECT clip{};
            assert(User::PointerClipActive() && User::PointerClip(&clip));
            if (clip.left || clip.top || clip.right != 800 || clip.bottom != 560)
                fprintf(stderr, "drag clip %d,%d,%d,%d\n", clip.left, clip.top, clip.right, clip.bottom);
            assert(clip.left == 0 && clip.top == 0 && clip.right == 800 && clip.bottom == 560);
        }
        auto dragging = Frame();
        // The frame is in the frame buffer, drawn with PATINVERT and USER's
        // gray pattern from the window corner : the
        // corner pixel is inverted, the one beside it is not.
        assert(screen_pixel(rect.left, rect.top) == (corner ^ 0xffffffu));
        assert(screen_pixel(rect.left + 1, rect.top) == beside);
        for (int i = 1; i <= 6; ++i) { Move(rect.right - 2 - i * 9, rect.bottom - 2 - i * 7); Pump(20); }
        Pump(40);
        auto outlined = Frame();
        // Without "show window contents while dragging" the window has not
        // moved yet, but the inverted outline is on the screen.
        Gdi::RECT during{};
        assert(User::GetWindowRect(window, &during));
        assert(during.right - during.left == width && during.bottom - during.top == height);
        assert(Differences(dragging, outlined, 0, 0, 800, 600) > 100);
        Release(rect.right - 2 - 54, rect.bottom - 2 - 42);
        Pump(200);
        Frame();
        // The release inverts the frame once more before the window moves.
        assert(screen_pixel(rect.left, rect.top) != (corner ^ 0xffffffu));
        // ClipCursor(NULL) at the end of the drag.
        assert(!User::PointerClipActive());
        Gdi::RECT sized{};
        assert(User::GetWindowRect(window, &sized));
        assert(sized.left == rect.left && sized.top == rect.top);
        assert(sized.right - sized.left == width - 54 && sized.bottom - sized.top == height - 42);
        Phase(window, 3);

        // A move drag of the caption.
        Press(sized.left + 120, sized.top + 10);
        Pump(60);
        for (int i = 1; i <= 5; ++i) { Move(sized.left + 120 + i * 8, sized.top + 10 + i * 6); Pump(20); }
        // The mouse driver's position stays inside the clip (USER over): below the work area the pointer stops on its last row.
        Move(sized.left + 160, 595);
        Pump(20);
        {
            Gdi::POINT at{};
            User::CursorPosition(&at);
            assert(at.x == sized.left + 160 && at.y == 559);
        }
        Release(sized.left + 160, sized.top + 40);
        Pump(200);
        Gdi::RECT moved{};
        assert(User::GetWindowRect(window, &moved));
        Phase(window, 4);
        if (moved.left != sized.left + 40 || moved.top != sized.top + 30)
            fprintf(stderr, "move drag: from %d,%d to %d,%d\n", sized.left, sized.top, moved.left, moved.top);
        assert(moved.left == sized.left + 40 && moved.top == sized.top + 30);
        assert(moved.right - moved.left == sized.right - sized.left);

        // The vertical bar. Its track is the column just right of the client
        // rectangle; the arrow at the bottom is one scrollbar metric high.
        Gdi::RECT client{};
        assert(User::GetClientRect(window, &client));
        Gdi::POINT origin{0, 0};
        assert(User::ScreenToClient(window, &origin));
        const int bar_left = -origin.x + client.right, bar_width = 17;
        const int bar_top = -origin.y, bar_bottom = -origin.y + client.bottom;
        const int bar_x = bar_left + bar_width / 2;

        // An arrow press that outlives the first repeat delay of 400 ms.
        auto quiet = Frame();
        Press(bar_x, bar_bottom - 4);
        Pump(80);
        auto pushed = Frame();
        assert(Differences(quiet, pushed, bar_left, bar_bottom - 20, bar_left + bar_width, bar_bottom) > 10);
        Pump(700);
        Release(bar_x, bar_bottom - 4);
        Pump(120);
        auto released = Frame();
        assert(Differences(quiet, released, bar_left, bar_top, bar_left + bar_width, bar_bottom) == 0);
        Phase(window, 5);

        // A page press below the thumb. The inverted region must be restored
        // when the button comes back up.
        quiet = Frame();
        const int page_y = (bar_top + bar_bottom) / 2 + 40;
        Press(bar_x, page_y);
        Pump(60);
        auto inverted = Frame();
        assert(Differences(quiet, inverted, bar_left, bar_top, bar_left + bar_width, bar_bottom) > 50);
        Release(bar_x, page_y);
        Pump(120);
        auto restored = Frame();
        assert(Differences(quiet, restored, 0, 0, 800, 600) == 0);
        Phase(window, 6);

        // A thumb drag. The thumb follows while the pointer stays near the bar
        // and snaps back to its starting pixel once it leaves the zone.
        // Put the thumb at the top of its travel so the press lands on it.
        {
            Win98Scroll::Info info{28, 4, 0, 0, 0, 0, 0};
            User::ScrollState(window, 1, 1, &info, 1);
        }
        Pump(60);
        quiet = Frame();
        const int thumb_y = bar_top + 17 + 3;
        Press(bar_x, thumb_y);
        Pump(40);
        for (int i = 1; i <= 5; ++i) { Move(bar_x, thumb_y + i * 12); Pump(20); }
        auto dragged = Frame();
        assert(Differences(quiet, dragged, bar_left, bar_top, bar_left + bar_width, bar_bottom) > 20);
        // Far to the left of the bar: outside two bar widths, so it snaps back.
        Move(bar_left - 4 * bar_width, thumb_y + 60);
        Pump(60);
        auto snapped = Frame();
        assert(Differences(quiet, snapped, bar_left, bar_top, bar_left + bar_width, bar_bottom) == 0);
        Move(bar_x, thumb_y + 60);
        Pump(40);
        Release(bar_x, thumb_y + 60);
        Pump(120);
        Phase(window, 7);

        // The drawn cursor. It is not part of the composition until a caller
        // asks for it, which is why no other pixel test sees it.
        auto without = Frame();
        User::ShowPointer(true);
        assert(User::PointerComposited());
        assert(User::PointerShape());
        app->MouseButtons(400, 300, 0);
        Pump(60);
        auto with = Frame();
        const unsigned drawn = Differences(without, with, 0, 0, 800, 600);
        assert(drawn > 40);
        // Everything it changed lies in the 32 by 32 cell at the hotspot.
        assert(Differences(without, with, 0, 0, 800, 300) +
               Differences(without, with, 0, 332, 800, 600) +
               Differences(without, with, 0, 300, 400, 332) +
               Differences(without, with, 432, 300, 800, 332) == 0);
        // ShowCursor(FALSE) takes it away again.
        User::ShowPointerCount(false);
        auto hidden = Frame();
        assert(Differences(without, hidden, 0, 0, 800, 600) == 0);
        User::ShowPointerCount(true);
        assert(User::PointerVisible());
        auto shown = Frame();
        assert(Differences(with, shown, 0, 0, 800, 600) == 0);
        if (argc == 4)
        {
            FILE *f = fopen(argv[3], "wb");
            assert(f);
            fprintf(f, "P6\n800 600\n255\n");
            const auto *p = User::Pixels();
            for (unsigned i = 0; i < 800 * 600; ++i)
            {
                unsigned char rgb[]{p[4 * i + 2], p[4 * i + 1], p[4 * i]};
                assert(fwrite(rgb, 3, 1, f) == 1);
            }
            fclose(f);
        }
        User::ShowPointer(false);

        assert(User::PostMessage(window, 0x8000, 99, 0));
        for (unsigned i = 0; i < 4000 && Alive(); ++i) app->Pump(16);
        if (application.Status() != X86::Application::Exited || application.ExitCode())
            fprintf(stderr, "FRAME guest: status=%u code=%u error=%s\n", application.Status(),
                    application.ExitCode(), application.Error());
        assert(application.Status() == X86::Application::Exited && !application.ExitCode());
        printf("FRAME TRACK guest PASS calls=%u callbacks=%u cursor pixels=%u\n",
               application.ApiCalls(), application.Callbacks(), drawn);
        app = nullptr;
    }
    assert(Gdi::LiveObjects() == before_objects);
    return 0;
}
