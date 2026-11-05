// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the implemented DrawFrameControl body. The recorded display
// target lives in src/user_draw.cpp, so the host comparison and the retained
// kernel replay measure exactly the same code.
#include "../src/win98_frame_control.h"

extern "C" unsigned frame_control_probe(unsigned char *pixels, Gdi::RECT *rect,
                                        unsigned type, unsigned state, int depth, int bw, int bh,
                                        unsigned menu_background, unsigned check_background,
                                        const unsigned *colors, unsigned *trace, unsigned *counters,
                                        int keep)
{
    Win98Frame::Probe probe;
    probe.pixels = pixels;
    for (unsigned index = 0; index < Win98Frame::ColorCount; ++index)
        probe.colors[index] = colors[index];
    const bool result = Win98Frame::Run(probe, rect, type, state, depth, bw, bh,
                                        menu_background, check_background, keep != 0);
    *trace = probe.trace;
    counters[0] = probe.created;
    counters[1] = probe.deleted;
    counters[2] = probe.cells_drawn;
    return result;
}
