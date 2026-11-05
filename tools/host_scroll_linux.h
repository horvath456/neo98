// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_wheel.h"
#include <algorithm>

namespace LinuxScroll
{
// X11/XWayland's SDL events express both axes in wheel notches. Fractions
// retain their distance; a quiet interval starts a new gesture. SDL2 does
// not expose finger-up or momentum flags on this backend.
struct Wheel
{
    HostWheel wheel;
    unsigned focus = 0;
    unsigned long long last = 0;
    bool fine = false;
    HostWheelStep Convert(double up, double right, bool held, unsigned target, double scale,
                          unsigned long long now)
    {
        HostWheelStep result;
        if (!std::isfinite(up) || !std::isfinite(right)) return result;
        up = std::clamp(up, -2048.0, 2048.0); right = std::clamp(right, -2048.0, 2048.0);
        const bool first = !last || now - last >= 400 || focus != target;
        if (first) { wheel.Reset(); fine = false; }
        focus = target; last = now;
        if (held)
        {
            result.steps = wheel.Steps(up, target);
            result.kind = result.steps ? HostWheelStep::Notches : HostWheelStep::Nothing;
            return result;
        }
        fine = fine || up != std::floor(up) || right != std::floor(right);
        if (fine)
            result.out = wheel.Points(up * HostWheel::PixelsPerNotch, right * HostWheel::PixelsPerNotch,
                                      scale, first, false);
        else { result.out = wheel.Notches(int(up)); result.out.x = int(right) * 120; }
        if (result.out.y || result.out.x) result.kind = HostWheelStep::Scroll;
        return result;
    }
};
}
