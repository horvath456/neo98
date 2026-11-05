// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER scrollbar mouse tracking, USER (SBTrackInit),
// USER (the modal loop), USER (SBTrackEnd), the two mouse filters
// USER and USER, the repeat timer USER and the pressed
// page rectangle USER. The state and geometry core it works on is
// win98_scroll_family.h.
#pragma once
#include "win98_scroll_family.h"
namespace Win98ScrollTrack
{
using U = unsigned;
using Geometry = Win98Scroll::Geometry;
// USER's tracked part, the word at its data 0E2Ah. The value is the SB_ code
// itself; the thumb gets the one code that is not a scroll command.
enum Part { LineUp = 0, LineDown = 1, PageUp = 2, PageDown = 3, Thumb = 4, None = 0xffff };
enum Code { SbLineUp = 0, SbLineDown = 1, SbPageUp = 2, SbPageDown = 3,
            SbThumbPosition = 4, SbThumbTrack = 5, SbTop = 6, SbBottom = 7, SbEndScroll = 8 };
// USER derives both delays from the double-click time: the stored interval
// is time*4/5, the repeat interval is that value divided by eight. With the
// default double-click time of 500 ms that is 400 ms and 50 ms.
inline U FirstDelay(U double_click) { return (double_click ? double_click : 500u) * 4 / 5; }
inline U RepeatDelay(U double_click) { return FirstDelay(double_click) >> 3; }
// The part a press lands on, on the bar's own axes. `disabled` carries the two
// ESB bits of this bar. Answers None where USER abandons the track attempt.
inline U Hit(const Geometry &g, int along, int across, U disabled)
{
    if (across < g.cross_start || across >= g.cross_end) return None;
    if (along < g.start || along >= g.end) return None;
    if ((disabled & 3) == 3) return None;                        //
    if (along < g.track_start) return (disabled & 1) ? None : LineUp;   //
    if (along >= g.track_end) return (disabled & 2) ? None : LineDown;  //
    if (along < g.thumb_start) return PageUp;
    if (along < g.thumb_end)
        return g.track_end - g.track_start <= g.thumb_end - g.thumb_start ? None : Thumb; //
    return PageDown;
}
struct Rect { int left, top, right, bottom; };
inline bool In(const Rect &r, int x, int y) { return r.left <= x && x < r.right && r.top <= y && y < r.bottom; }
// USER. The rectangle a pressed arrow or page occupies, on the
// bar's own axes projected back to screen coordinates.
inline Rect PartRect(const Geometry &g, U part, U vertical)
{
    int a = g.start, z = g.end;
    if (part == LineUp) { a = g.start; z = g.track_start; }
    else if (part == LineDown) { a = g.track_end; z = g.end; }
    else if (part == PageUp) { a = g.track_start; z = g.thumb_start; }
    else if (part == PageDown) { a = g.thumb_end; z = g.track_end; }
    if (vertical) return {g.cross_start, a, g.cross_end, z};
    return {a, g.cross_start, z, g.cross_end};
}
// USER. Every repaint re-clips the pressed page region against the
// live thumb. This is the step that stops a page repeat once the thumb has
// grown under the pointer, and the one whose absence left the inverted region
// standing.
inline bool ClipPage(Rect &r, const Geometry &g, U part, U vertical)
{
    if (part != PageUp && part != PageDown) return false;
    int &start = vertical ? r.top : r.left;
    int &end = vertical ? r.bottom : r.right;
    if (part == PageUp) end = g.thumb_start; else start = g.thumb_end;
    return end > start;
}
// USER. The zone a thumb drag may not leave: the whole bar
// rectangle inflated by twice its own short side across and twice the scroll
// metric along.
inline Rect SnapZone(const Geometry &g, U vertical, int scroll_metric)
{
    Rect r = vertical ? Rect{g.cross_start, g.start, g.cross_end, g.end}
                      : Rect{g.start, g.cross_start, g.end, g.cross_end};
    const int span = g.cross_end - g.cross_start;
    const int dx = vertical ? span * 2 : scroll_metric * 2;
    const int dy = vertical ? scroll_metric * 2 : span * 2;
    r.left -= dx; r.right += dx; r.top -= dy; r.bottom += dy;
    return r;
}
// USER, on the already ported rounded unsigned divide.
inline U Position(const Win98Scroll::Bar &b, const Geometry &g, int pixel)
{
    const U top = b.maximum - (b.page ? b.page - 1 : 0);
    if (pixel <= g.track_start) return b.minimum;
    const int travel = (g.track_end - g.track_start) - (g.thumb_end - g.thumb_start);
    if (travel <= 0 || pixel >= g.track_start + travel) return top;
    U value = 0;
    if (!Win98Scroll::Ratio(U(pixel - g.track_start), top - b.minimum, U(travel), value))
        return b.position;
    return b.minimum + value;
}
} // namespace Win98ScrollTrack
