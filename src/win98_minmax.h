// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER: MINMAXINFO defaults, the WM_GETMINMAXINFO callback and the
// work-area correction that follows it. Phase one runs before the window procedure
// sees the structure, phase two afterwards. Offsets name words of the analyzed
// USER.EXE data segment, not ARM kernel addresses.
#pragma once
namespace Win98MinMax
{
using U = unsigned;
struct Rect
{
    int left, top, right, bottom;
};
struct Point
{
    int x, y;
};
struct Info
{
    Point reserved, max_size, max_position, min_track, max_track;
};
struct Environment
{
    virtual unsigned short Word(U offset) const = 0; // Shared USER metric words.
    virtual void MonitorRect(Rect &) const = 0;      // Monitor record +0x10.
    virtual void WorkArea(Rect &) const = 0;         // Monitor record +0x18.
    virtual bool MonitorReserved() const = 0;        // Monitor record +0x26, nonzero suppresses it.
    virtual bool ParentIsDesktop() const = 0;        // WND +0x08 against the desktop.
    virtual void ParentClientRect(Rect &) const = 0; // USER for child windows.
    virtual U Style() const = 0;                     // WND +0x30.
    virtual U ExtendedStyle() const = 0;             // WND +0x34.
    virtual bool AppWindow() const = 0;              // WND +0x2c bit 3, an internal USER flag.
    virtual bool Maximizable() const = 0;            // USER gate before the correction.
    virtual void SetWorkAreaMaximized(bool) = 0;     // WND +0x39 bit 1.
};
// Complete USER: the frame thickness of a window in border units.
inline int BorderUnits(U style, U extended, bool frame, bool extra, U border_setting)
{
    int units = 0;
    if (frame)
    {
        if (extended & 0x00000100u)
            units = 2;
        else if (extended & 0x00020000u)
            units = 1;
        if ((style & 0x00c00000u) || (extended & 1u))
            ++units;
        if (style & 0x00040000u)
            units += int(border_setting & 0xffff);
    }
    if (extra && (extended & 0x00000200u))
        units += 2;
    return units;
}
inline int Signed(U word) { return (word & 0x8000) ? int(word & 0xffff) - 65536 : int(word & 0xffff); }
inline int Word16(int value) { return Signed(U(value) & 0xffff); }
// USER, the state the original hands to WM_GETMINMAXINFO.
inline void Defaults(Environment &e, Info &info)
{
    info.reserved = {Signed(e.Word(0x350)), Signed(e.Word(0x352))};
    Rect r;
    if (e.ParentIsDesktop())
        e.MonitorRect(r);
    else
        e.ParentClientRect(r);
    const int multiplier = BorderUnits(e.Style(), e.ExtendedStyle(), true, false, e.Word(0x1014));
    const int x = Word16(multiplier * Signed(e.Word(0x2e8)));
    const int y = Word16(multiplier * Signed(e.Word(0x2ea)));
    r = {r.left - x, r.top - y, r.right + x, r.bottom + y}; // USER InflateRect.
    info.max_size = {Word16(r.right - r.left), Word16(r.bottom - r.top)};
    info.max_position = {r.left, r.top};
    // The original consults a per-window record for a remembered maximized position;
    // that lookup is an environment concern and is not implemented here.
    const int border_x = Word16(multiplier * Signed(e.Word(0x338)));
    const int border_y = Word16(multiplier * Signed(e.Word(0x33a)));
    if ((e.Style() & 0x00c00000u) && !(e.ExtendedStyle() & 0x80u))
        info.min_track = {Signed(e.Word(0x322)), Signed(e.Word(0x324))};
    else
        info.min_track = {border_x > Signed(e.Word(0x338)) ? border_x : Signed(e.Word(0x338)),
                          border_y > Signed(e.Word(0x33a)) ? border_y : Signed(e.Word(0x33a))};
    info.max_track = {Signed(e.Word(0x354)), Signed(e.Word(0x356))};
}
// USER, applied after the window procedure returned from WM_GETMINMAXINFO.
inline void Adjust(Environment &e, Info &info)
{
    if (!e.Maximizable())
        return;
    info.reserved = {Signed(e.Word(0x350)), Signed(e.Word(0x352))};
    if (!e.ParentIsDesktop())
        return;
    Rect monitor, work;
    e.MonitorRect(monitor);
    e.WorkArea(work);
    // A maximizable application window takes the work area; everything else the monitor.
    const bool work_area = (e.Style() & 0x00010000u) && e.AppWindow() && !e.MonitorReserved();
    Rect chosen = work_area ? work : monitor;
    if (info.max_size.x < monitor.right - monitor.left ||
        info.max_size.y < monitor.bottom - monitor.top)
    {
        // The caller asked for less than a full monitor; only the origin moves.
        e.SetWorkAreaMaximized(false);
    }
    else
    {
        e.SetWorkAreaMaximized(true);
        // a caller whose caption ends at or above the top of the
        // monitor and whose bottom reaches the monitor's bottom wants the whole
        // monitor. Both comparisons are against rcMonitor (11B4h, 11B8h), not
        // the work area: with a work area whose top is not the monitor's, the
        // default position of minus one frame is not such a caller.
        if (!(info.max_position.y + Signed(e.Word(0x2e6)) > monitor.top ||
              info.max_position.y + info.max_size.y < monitor.bottom))
            chosen = monitor;
        info.max_size.x =
            Word16(info.max_size.x + (chosen.right - chosen.left) - (monitor.right - monitor.left));
        info.max_size.y =
            Word16(info.max_size.y + (chosen.bottom - chosen.top) - (monitor.bottom - monitor.top));
    }
    info.max_position.x = Word16(info.max_position.x + chosen.left);
    info.max_position.y = Word16(info.max_position.y + chosen.top);
}
} // namespace Win98MinMax
