// SPDX-License-Identifier: GPL-2.0-or-later
// The coordinate conversion between the host's canvas (tools/neo98_host.cpp) and the guest's own current screen. The canvas has the
// size of the desktop mode (User::PixelsSize, 800x600 without a registry
// entry), so on the desktop both are the same; they differ while a program
// has set a mode of another size. A DirectDraw or GDI mode switch (ChangeDisplaySettingsA, DDRAW16
// SetDisplayMode) resizes the guest's own screen: SM_CXSCREEN/SM_CYSCREEN and
// every window rect follow the new mode (Application::ApplyScreenMetrics,
// src/user_screen.inc), but the host window stays the fixed canvas; the
// compositor stretches the mode onto it exactly the way a fixed-resolution
// monitor stretches a smaller mode (src/win98_display_modes.h,
// Win98DisplayModes::Publish). A guest pointer position is given in the
// mode's own pixels, and a position read from the host window is in canvas
// pixels; the two only coincide in the native 800x600 mode.
// Before this header existed, tools/neo98_host.cpp fed canvas pixels to
// Application::MouseButtons and the guest's SetCursorPos target straight to
// SDL_WarpMouseInWindow, unconverted. A program that switched to a smaller
// mode (Moorhuhn's 640x480) then read its own mouse relative to its own
// screen while the host moved the real pointer in canvas pixels, a mismatch
// of up to (800-640)/2, (600-480)/2 pixels, matching the observed
// ~100 pixel offset exactly (sqrt(80*80+60*60) = 100). Both directions are
// plain integer math with no SDL and no Application dependency, so they are
// a host unit test independent of the interactive host
// (tools/test_guest_screen_scale_native.cpp).
#pragma once
namespace GuestScreenScale
{
constexpr int CanvasWidth = 800, CanvasHeight = 600;

// A host window pixel (0..canvas_width-1, 0..canvas_height-1) to the guest's own
// current screen. Clamped to the guest screen, the way a real pointer never
// leaves its own monitor.
inline void CanvasToGuest(int &x, int &y, int guest_width, int guest_height,
                          int canvas_width = CanvasWidth, int canvas_height = CanvasHeight)
{
    if (guest_width <= 0 || guest_height <= 0 || canvas_width <= 0 || canvas_height <= 0)
        return;
    x = (int)((long long)x * guest_width / canvas_width);
    y = (int)((long long)y * guest_height / canvas_height);
    if (x < 0) x = 0;
    if (x >= guest_width) x = guest_width - 1;
    if (y < 0) y = 0;
    if (y >= guest_height) y = guest_height - 1;
}

// The inverse: a point the guest gave in its own current screen (a
// SetCursorPos argument) to the host window pixel it is actually shown at.
inline void GuestToCanvas(int &x, int &y, int guest_width, int guest_height,
                          int canvas_width = CanvasWidth, int canvas_height = CanvasHeight)
{
    if (guest_width <= 0 || guest_height <= 0 || canvas_width <= 0 || canvas_height <= 0)
        return;
    x = (int)((long long)x * canvas_width / guest_width);
    y = (int)((long long)y * canvas_height / guest_height);
    if (x < 0) x = 0;
    if (x > canvas_width - 1) x = canvas_width - 1;
    if (y < 0) y = 0;
    if (y > canvas_height - 1) y = canvas_height - 1;
}

// The host window for a canvas: `scale` points per canvas pixel, made smaller
// until it fits the usable area of the Mac screen (both in points). A canvas
// larger than that area at scale 1 is fitted with its aspect ratio kept; the
// window then shows it scaled down. Width and height of zero mean no screen
// is known, and the window keeps canvas times scale.
inline void WindowForCanvas(int canvas_width, int canvas_height, int scale, int usable_width,
                            int usable_height, int &window_width, int &window_height)
{
    if (scale < 1) scale = 1;
    for (; scale > 1; --scale)
        if (usable_width <= 0 || usable_height <= 0 ||
            (canvas_width * scale <= usable_width && canvas_height * scale <= usable_height))
            break;
    window_width = canvas_width * scale;
    window_height = canvas_height * scale;
    if (usable_width <= 0 || usable_height <= 0) return;
    if (window_width > usable_width || window_height > usable_height)
    {
        if ((long long)usable_width * canvas_height <= (long long)usable_height * canvas_width)
        {
            window_width = usable_width;
            window_height = (int)((long long)usable_width * canvas_height / canvas_width);
        }
        else
        {
            window_height = usable_height;
            window_width = (int)((long long)usable_height * canvas_width / canvas_height);
        }
    }
}
} // namespace GuestScreenScale
