// SPDX-License-Identifier: GPL-2.0-or-later
// Host bridge for the implemented caption button measurement, USER.
// The border unit count comes from the already ported Win98Nonclient body, so
// the comparison covers the same split the native frame painter uses.
#include "../src/win98_caption_buttons.h"
#include "../src/win98_nonclient.h"

namespace {
struct Metrics : Win98Nonclient::Environment {
    unsigned short words[0x1100] = {};
    unsigned short Word(unsigned offset) const override
    {
        return offset / 2 < 0x1100 ? words[offset / 2] : 0;
    }
    unsigned short ExpectedVersion() const override { return 0x400; }
    Win98Nonclient::WindowState ReadWindow(unsigned) const override { return {}; }
    void WriteWindow(unsigned, const Win98Nonclient::WindowState &) override {}
    bool MenuValid(unsigned) override { return false; }
    int MenuHeight(unsigned, unsigned, int, int, int) override { return 0; }
    void NotifyReorder(unsigned) override {}
};
}

// values: border_x, border_y, dialog_x, dialog_y, size_x, size_y, edge_x,
// edge_y, small_x, small_y, border_width.
extern "C" unsigned caption_button_probe(const int *window, unsigned style, unsigned extended,
                                         unsigned slot, const int *values, int close_enabled,
                                         int *out_rect, unsigned *out_command, unsigned *out_cell,
                                         int *out_x, int *out_y)
{
    Metrics environment;
    environment.words[0x1014 / 2] = (unsigned short)values[10];
    Win98CaptionButton::Metrics m;
    m.border_x = values[0];
    m.border_y = values[1];
    m.dialog_x = values[2];
    m.dialog_y = values[3];
    m.size_x = values[4];
    m.size_y = values[5];
    m.edge_x = values[6];
    m.edge_y = values[7];
    m.small_x = values[8];
    m.small_y = values[9];
    const unsigned units = Win98Nonclient::BorderUnits(environment, style, extended, 1, 0);
    Gdi::RECT box{window[0], window[1], window[2], window[3]};
    Win98CaptionButton::Result result;
    const bool drawn = Win98CaptionButton::Measure(m, box, style, extended, slot, units,
                                                  close_enabled != 0, result);
    out_rect[0] = result.rect.left;
    out_rect[1] = result.rect.top;
    out_rect[2] = result.rect.right;
    out_rect[3] = result.rect.bottom;
    *out_command = result.command;
    *out_cell = result.cell;
    *out_x = result.x;
    *out_y = result.y;
    return drawn;
}
