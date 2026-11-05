// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gdi.h"
#include "../src/user.h"
#include "../src/gdi_truetype.h"
#include <cstdlib>
#include <cassert>
#include <cstdio>

using U = unsigned;

// This bitmap-only provider test must never enter the TrueType driver.
namespace GdiTrueType { bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); } }

int main()
{
    assert(User::Initialize());
    assert(User::RegisterClass("WindowFromDCNative", 1, 0, 0));

    const U custom = User::CreateWindow("WindowFromDCNative", "custom",
                                        0x90000000u, 20, 20, 120, 80, 0, 0, 0);
    assert(custom); User::CompleteCreation(custom);
    // DDRAW16 receives the low word after GDI generations exceed 16 bits.
    for (unsigned i = 0; i < 40; ++i) {
        const U churn = Gdi::CreateCompatibleDC(); assert(churn);
        assert(Gdi::DeleteDC(churn));
    }
    U dc = User::GetDC(custom); assert(dc && dc > 0xffff);
    assert(Gdi::FullHandle(dc & 0xffff) == dc);
    assert(User::WindowFromDC(dc & 0xffff) == custom);
    assert(User::WindowFromDC(dc) == custom);

    // A paint lease and a retained GetDC lease can coexist on the same cached
    // canvas. The owner remains valid until the last lease is released.
    User::InvalidateRect(custom, nullptr, true);
    User::Paint paint{}; assert(User::BeginPaint(custom, paint));
    assert(User::WindowFromDC(paint.dc) == custom);
    assert(User::WindowFromDC(dc) == custom);
    assert(User::EndPaint(custom, paint));
    assert(User::WindowFromDC(dc) == custom);
    assert(User::ReleaseDC(custom, dc));
    assert(User::WindowFromDC(dc) == 0);
    assert(User::WindowFromDC(dc & 0xffff) == 0);

    // A direct screen lease (GetDC(NULL)) has no window owner. Every window,
    // a native control as well, gets a DCE of its own on the screen (the
    // one-screen design), and WindowFromDC names it while it is held.
    const U screen = User::GetDC(0); assert(screen);
    assert(User::WindowFromDC(screen) == 0);
    const U button = User::CreateWindow("BUTTON", "button", 0x90000000u,
                                        40, 40, 100, 30, 0, 0, 0);
    assert(button); User::CompleteCreation(button);
    const U button_dc = User::GetDC(button); assert(button_dc && button_dc != screen);
    assert(User::WindowFromDC(button_dc) == button);
    assert(User::ReleaseDC(button, button_dc));
    assert(User::WindowFromDC(button_dc) == 0);
    assert(User::WindowFromDC(screen) == 0);
    assert(User::ReleaseDC(0, screen));
    assert(User::WindowFromDC(screen) == 0);

    // The desktop has DCEs of its own as well. They are distinct from the
    // direct screen lease returned for NULL and keep the desktop HWND.
    const U desktop = User::GetDesktopWindow();
    const U desktop_dc = User::GetDC(desktop);
    assert(desktop && desktop_dc && User::WindowFromDC(desktop_dc) == desktop);
    assert(User::ReleaseDC(desktop, desktop_dc));
    assert(User::WindowFromDC(desktop_dc) == 0);

    // Across tasks the DCE cache grows beyond the former eight slots.
    U nested[32] = {};
    for (unsigned i = 0; i < 32; ++i) {
        User::BindTask(0,100+i);
        nested[i] = User::GetDC(button);
        assert(nested[i] && User::WindowFromDC(nested[i]) == button);
        for (unsigned k = 0; k < i; ++k) assert(nested[k] != nested[i]);
    }
    for (unsigned i = 0; i < 32; ++i) {
        assert(User::ReleaseDC(button, nested[31 - i]));
        assert(User::WindowFromDC(nested[31 - i]) == 0);
        if (i != 31) assert(User::WindowFromDC(nested[0]) == button);
    }

    User::BindTask(0,0);

    // Memory and standalone display DCs never enter USER's window mapping.
    const U memory = Gdi::CreateCompatibleDC(); assert(memory);
    assert(User::WindowFromDC(memory) == 0);
    assert(Gdi::DeleteDC(memory));
    const U display_source = User::GetDC(0); assert(display_source);
    const U display = Gdi::CreateDisplayDC(display_source, false); assert(display);
    assert(User::WindowFromDC(display) == 0);
    assert(Gdi::DeleteDC(display));
    assert(User::ReleaseDC(0, display_source));

    // Destruction invalidates a window's DC. Reusing a
    // window slot must not make an old DC point at the new generation.
    dc = User::GetDC(custom); assert(dc);
    assert(User::DestroyWindow(custom));
    assert(User::WindowFromDC(dc) == 0);
    assert(!User::ReleaseDC(custom, dc));
    const U replacement = User::CreateWindow("WindowFromDCNative", "replacement",
                                              0x90000000u, 20, 20, 120, 80, 0, 0, 0);
    assert(replacement && replacement != custom); User::CompleteCreation(replacement);
    const U replacement_dc = User::GetDC(replacement); assert(replacement_dc);
    assert(User::WindowFromDC(dc) == 0);
    assert(User::WindowFromDC(replacement_dc) == replacement);
    assert(User::ReleaseDC(replacement, replacement_dc));

    // A control's DC is a DCE of its own as well; it goes with the window.
    const U stale_control = User::GetDC(button); assert(stale_control);
    assert(User::DestroyWindow(button));
    assert(User::WindowFromDC(stale_control) == 0);
    assert(!User::ReleaseDC(button, stale_control));
    assert(User::DestroyWindow(replacement));
    User::Shutdown();
    Gdi::ShutdownBitmaps();
    std::puts("WINDOW-FROM-DC PASS: live window/paint/screen owners, memory/display DCs, release and generation invalidation");
}
