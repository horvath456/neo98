// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER metrics: native setting storage replaces its shared data segment.
#pragma once
namespace Win98Metrics
{
using U = unsigned;
struct Environment
{
    virtual unsigned short Word(U offset) const = 0;
    virtual void SetWord(U offset, unsigned short value) = 0;
    virtual unsigned short ExpectedVersion() const = 0;
    virtual U PenWindows() = 0;
};
struct NonClient
{
    U border_width, scroll_width, scroll_height, caption_width, caption_height;
    U small_caption_width, small_caption_height, menu_width, menu_height;
};
inline int Signed(U word) { return (word & 0x8000) ? int(word & 0xffff) - 65536 : int(word & 0xffff); }
// Complete USER32; the Pen Windows lookup and cache
// are an environment provider. USER shares the table.
inline int Query(Environment &e, int index)
{
    if (index < 0 || index > 81)
        return 0;
    if (index == 41)
        return static_cast<int>(e.PenWindows());
    int value = Signed(e.Word(0x2de + U(index) * 2));
    static constexpr signed char compatibility[22] = {0, 0, 1, 1, 1,  0, 0,  -1, -1, 0, 0,
                                                      0, 0, 0, 0, -1, 0, -1, 0,  0,  1, 1};
    if (index < 22 && compatibility[index] && Signed(e.ExpectedVersion()) < 0x400)
        value += compatibility[index];
    return value;
}
// Complete USER, called after work-area/nonclient settings change.
inline void Recompute(Environment &e)
{
    const U width = U(e.Word(0x11be)) - e.Word(0x11ba);
    const U height = U(e.Word(0x11c0)) - e.Word(0x11bc);
    e.SetWord(0x2fe, width);
    e.SetWord(0x358, U(e.Word(0x31e)) * 2 + width);
    e.SetWord(0x300, height - e.Word(0x2e6));
    e.SetWord(0x35a, U(e.Word(0x320)) * 2 + height);
}
// Complete arithmetic routine USER and its USER15 helper.
// NONCLIENTMETRICS fields enter as native values; the original uses their low WORDs.
// Font selection/measurement and validation of supplied settings precede this routine.
inline void ApplyNonClientMetrics(Environment &e, const NonClient &n)
{
    const U scroll_width = n.scroll_width & 0xffff, scroll_height = n.scroll_height & 0xffff;
    e.SetWord(0x2e4, scroll_width);
    e.SetWord(0x2e2, scroll_width);
    e.SetWord(0x308, scroll_height);
    e.SetWord(0x306, scroll_height);
    e.SetWord(0x2f2, scroll_height);
    e.SetWord(0x2f0, scroll_height);
    const U caption_width = n.caption_width & 0xffff, caption_height = n.caption_height & 0xffff;
    e.SetWord(0x31a, caption_width);
    e.SetWord(0x31c, caption_height);
    const U caption = (U(e.Word(0x2ea)) + caption_height) & 0xffff;
    e.SetWord(0x2e6, caption);
    U small_width = (caption_width - e.Word(0x338)) & 0xfffe;
    const U small_height = (caption_height - e.Word(0x33a)) & 0xfffe;
    if (Signed(small_width) > Signed(small_height))
        small_width = small_height;
    e.SetWord(0x340, small_width);
    e.SetWord(0x342, small_width);
    e.SetWord(0x346, n.small_caption_width);
    e.SetWord(0x348, n.small_caption_height);
    e.SetWord(0x344, (n.small_caption_height & 0xffff) + e.Word(0x2ea));
    e.SetWord(0x34a, n.menu_width);
    e.SetWord(0x34c, n.menu_height);
    e.SetWord(0x2fc, (n.menu_height & 0xffff) + e.Word(0x2ea));
    e.SetWord(0x1014, n.border_width);
    const U frame_x = (((n.border_width + 1) & 0xffff) * e.Word(0x2e8) + e.Word(0x338)) & 0xffff;
    const U frame_y = (((n.border_width + 1) & 0xffff) * e.Word(0x2ea) + e.Word(0x33a)) & 0xffff;
    e.SetWord(0x31e, frame_x);
    e.SetWord(0x320, frame_y);
    const U minimum_width =
        ((U(e.Word(0xedc)) * 2 + frame_x) * 2 + caption_width * 3 + caption_height + e.Word(0x6e8)) & 0xffff;
    const U minimum_height = (caption + frame_y * 2) & 0xffff;
    e.SetWord(0x322, minimum_width);
    e.SetWord(0x324, minimum_height);
    e.SetWord(0x354, U(e.Word(0x6e8)) + frame_x * 2 + e.Word(0x37a));
    e.SetWord(0x356, frame_y * 2 + e.Word(0x37c) + e.Word(0x6ea));
    e.SetWord(0x316, minimum_width);
    e.SetWord(0x318, minimum_height);
    e.SetWord(0x352, U(e.Word(0x2ee)) * 2 + caption_height);
    Recompute(e);
}
} // namespace Win98Metrics
