// SPDX-License-Identifier: GPL-2.0-or-later
// ctypes bridge for tools/test_button_layout_original.py.
#include "../src/win98_button_layout.h"
// in: style, win40, client right, client bottom, zero width, text width,
// text height (with the extra point), pressed. out: x, y, focus drawn,
// focus left, top, right, bottom.
extern "C" void button_layout(const int *in, int *out)
{
    using namespace Win98ButtonLayout;
    const Metrics m{2, 2, 1, 1, 13};
    const Rect client{0, 0, in[2], in[3]};
    const unsigned style = unsigned(in[0]);
    const Rect content = Content(style, in[1] != 0, client, in[4], m);
    int x = 0, y = 0;
    Caption(style, content, in[5], in[6], in[7] && PushKind(style), m, x, y);
    Rect focus{};
    const bool drawn = Focus(style, client, content, x, y, in[5], in[6], m, focus);
    out[0] = x; out[1] = y; out[2] = drawn;
    out[3] = focus.left; out[4] = focus.top; out[5] = focus.right; out[6] = focus.bottom;
}
