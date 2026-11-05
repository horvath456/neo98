// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside USER's native window provider, after the scroll drawing.
// The parts of the nine USER32 information queries
// that only the window record knows. Everything those queries can read through
// a public entry stays on the caller's side.

// USER copies WND+18h, the client rectangle in screen coordinates.
// GetClientRect answers the same rectangle moved to its own corner, so the
// query needs this one separately.
bool ClientScreenRect(HWND h, Gdi::RECT *out)
{
    auto *w = Find(h);
    if (!w || !out) return false;
    const auto o = Origin(*w);
    *out = {o.x + w->client.left, o.y + w->client.top,
            o.x + w->client.right, o.y + w->client.bottom};
    return true;
}

// USER asks whether the frame really carries a caption band now
// (WFCPRESENT), not only whether the style would ask for one, and
// takes the band out of the window rectangle. The port lays the band out when
// it measures the frame, so the answer is the measured rectangle.
bool CaptionBar(HWND h, Gdi::RECT *out)
{
    auto *w = Find(h);
    if (!w || !out) return false;
    if (w->caption.right <= w->caption.left || w->caption.bottom <= w->caption.top)
        return false;
    const auto o = Origin(*w);
    *out = At(w->caption, o);
    return true;
}

// USER reads the pressed bits of the caption buttons out of the window
// word at 39h. The port keeps the same one-button-at-a-time state in
// pressed_hit, and a button counts as pressed only while the pointer is on it.
unsigned CaptionPressed(HWND h)
{
    auto *w = Find(h);
    return w && w->pressed_inside ? w->pressed_hit : 0;
}

// One bar of one window: whether it is
// laid out at all, where it sits on the screen, how long its arrow buttons and
// its thumb are, which ends are disabled and which part is held down.
bool ScrollBarLayout(HWND h, int object, ScrollBarLayoutView &out)
{
    auto *w = Find(h);
    if (!w) return false;
    out = ScrollBarLayoutView{};
    Gdi::RECT rect{};
    Win98Scroll::Geometry g{};
    unsigned vertical = 0;
    if (object == -4)
    {
        // OBJID_CLIENT names a SCROLLBAR control, one bar filling its window.
        // Windows 98 reads the control's own record without checking the class.
        if (!ScrollControlGeometry(h, *w, rect, g, vertical)) return true;
    }
    else
    {
        vertical = object == -5 ? 1u : 0u;
        // without the style the bar is invisible, without the
        // nonclient flag it is only offscreen.
        out.styled = (w->style & (vertical ? 0x00200000u : 0x00100000u)) != 0;
        if (!out.styled) return true;
        if (!ScrollGeometry(h, *w, vertical, Origin(*w), rect, g)) return true;
    }
    out.present = true;
    out.vertical = vertical;
    out.bar = vertical ? Gdi::RECT{g.cross_start, g.start, g.cross_end, g.end}
                       : Gdi::RECT{g.start, g.cross_start, g.end, g.cross_end};
    out.line = g.track_start - g.start;
    out.thumb_start = g.thumb_start - g.start;
    out.thumb_end = g.thumb_end - g.start;
    out.empty_first = g.track_start == g.thumb_start;
    out.empty_last = g.thumb_end == g.track_end;
    if (!w->scroll_allocated) { w->scroll = Win98Scroll::Window{}; w->scroll_allocated = true; }
    out.disabled = (w->scroll.disabled >> (vertical * 2)) & 3;
    // Windows 98 keeps one held page region and one pushed arrow. The
    // bits are the ones USER keeps in the window word at 3Ah: line up, page
    // up, page down, line down.
    if (scroll_press.window == h && scroll_press.vertical == vertical)
        out.pressed |= scroll_press.part == Win98ScrollTrack::PageUp ? 2u : 4u;
    if (arrow_window == h && arrow_vertical == vertical && arrow_part != Win98ScrollTrack::None)
        out.pressed |= arrow_part == Win98ScrollTrack::LineUp ? 1u : 8u;
    return true;
}
