// SPDX-License-Identifier: GPL-2.0-or-later
// Native write branches of Win98 USER.EXE's SystemParametersInfo service.
// decodes fWinIni and dispatches through the table.
// The setters themselves live in USER segment 21 and are reached.
// Offsets name words of the analyzed USER.EXE data segment, not ARM kernel addresses.
#pragma once
namespace Win98ParametersWrite
{
using U = unsigned;
struct Rect
{
    int left, top, right, bottom;
};
// USER CopyRect, IntersectRect, EqualRect.
inline void CopyRect(Rect &destination, const Rect &source) { destination = source; }
inline bool EqualRect(const Rect &a, const Rect &b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool IntersectRect(Rect &destination, const Rect &a, const Rect &b)
{
    const int left = a.left > b.left ? a.left : b.left, top = a.top > b.top ? a.top : b.top;
    const int right = a.right < b.right ? a.right : b.right;
    const int bottom = a.bottom < b.bottom ? a.bottom : b.bottom;
    if (left >= right || top >= bottom)
    {
        destination = {0, 0, 0, 0};
        return false;
    }
    destination = {left, top, right, bottom};
    return true;
}
// The public lpvParam as the original sees it: a far pointer that may be NULL or
// carry the wallpaper sentinels -1 and -2.
struct Parameter
{
    const unsigned char *data = nullptr; // Structure bytes, when the caller supplied a buffer.
    const char *text = nullptr;          // Wallpaper/pattern string, when one was supplied.
    U capacity = 0;
    U sentinel = 0;   // 0xffffffff or 0xfffffffe reproduce the original pointer comparisons.
    bool null = true; // The original tests the selector word of the far pointer.
};
struct Environment
{
    // Monitor record USER 15 data 0x11a2; rcMonitor at +0x10, rcWork at +0x18.
    virtual void MonitorRect(Rect &) const = 0;
    virtual void WorkArea(Rect &) const = 0;
    virtual void SetWorkArea(const Rect &) = 0;
    virtual void RecomputeMetrics() = 0;                      // USER.
    virtual void RepositionWindows(const Rect &previous) = 0;  // USER.
    virtual bool WriteProfileInt(U identifier, int value) = 0; // USER.
    virtual bool WriteProfileFont(U identifier, const unsigned char *logfont) = 0; // USER.
    virtual void ApplyNonClientMetrics(const unsigned char *) = 0; // USER.
    virtual void ApplyMinimizedMetrics(const unsigned char *) = 0; // USER.
    virtual void ApplyIconMetrics(const unsigned char *) = 0;      // USER.
    virtual U CreateIconTitleFont(const unsigned char *logfont) = 0; // USER.
    virtual U IconTitleFont() const = 0;                             // USER data 0x1b0.
    virtual void SetIconTitleFont(U font) = 0;
    virtual void DeleteFont(U font) = 0;
    virtual bool FontObject(U font, unsigned char (&logfont)[50]) const = 0; // GDI GetObject.
    virtual U AppVersion() const = 0;                       // USER.
    virtual void RepaintDesktopSetting(U first, U second) = 0; // USER, original push order.
    virtual void RepaintDesktop() = 0;                      // USER.
    virtual bool SetDeskWallpaper(const Parameter &) = 0;   // USER.
    virtual bool SetDeskPattern(const Parameter &) = 0; // USER.
    virtual bool WriteProfileText(U identifier, const char *value) = 0; // USER.
    virtual bool SectionString(U identifier, char *out, U size) = 0;    // USER.
    // USER: LoadString of the section name, then the WM_WININICHANGE broadcast.
    virtual void Broadcast(U action, const char *section) = 0;
};
namespace Detail
{
inline U Word(const unsigned char *p, U offset)
{
    return U(p[offset]) | (U(p[offset + 1]) << 8);
}
inline U Read32(const unsigned char *p, U offset)
{
    return Word(p, offset) | (Word(p, offset + 2) << 16);
}
inline int Signed(U word) { return (word & 0x8000) ? int(word & 0xffff) - 65536 : int(word & 0xffff); }
// USER. The original pushes the low WORD of every NONCLIENTMETRICS integer.
inline bool SetNonClientMetrics(Environment &e, const unsigned char *m, bool update_ini)
{
    e.ApplyNonClientMetrics(m);
    if (!update_ini)
        return false;
    bool written = e.WriteProfileInt(0x74, Signed(Word(m, 4)));
    written |= e.WriteProfileInt(0x84, Signed(Word(m, 8)));
    written |= e.WriteProfileInt(0x85, Signed(Word(m, 0xc)));
    written |= e.WriteProfileInt(0x75, Signed(Word(m, 0x10)));
    written |= e.WriteProfileInt(0x76, Signed(Word(m, 0x14)));
    written |= e.WriteProfileInt(0x78, Signed(Word(m, 0x54)));
    written |= e.WriteProfileInt(0x79, Signed(Word(m, 0x58)));
    written |= e.WriteProfileInt(0x7b, Signed(Word(m, 0x98)));
    written |= e.WriteProfileInt(0x7c, Signed(Word(m, 0x9c)));
    written |= e.WriteProfileFont(0x77, m + 0x18);
    written |= e.WriteProfileFont(0x7a, m + 0x5c);
    written |= e.WriteProfileFont(0x7d, m + 0xa0);
    written |= e.WriteProfileFont(0x88, m + 0xdc);
    written |= e.WriteProfileFont(0x89, m + 0x118);
    return written;
}
// USER.
inline bool SetMinimizedMetrics(Environment &e, const unsigned char *m, bool update_ini)
{
    e.ApplyMinimizedMetrics(m);
    if (!update_ini)
        return false;
    bool written = e.WriteProfileInt(0x7e, Signed(Word(m, 4)));
    written |= e.WriteProfileInt(0x7f, Signed(Word(m, 8)));
    written |= e.WriteProfileInt(0x80, Signed(Word(m, 0xc)));
    written |= e.WriteProfileInt(0x82, Signed(Word(m, 0x10)));
    return written;
}
// USER. Unlike its two neighbours this one returns only the last write.
inline bool SetIconMetrics(Environment &e, const unsigned char *m, bool update_ini)
{
    e.ApplyIconMetrics(m);
    if (!update_ini)
        return false;
    e.WriteProfileInt(0x40, Signed(Word(m, 4)));
    e.WriteProfileInt(0x41, Signed(Word(m, 8)));
    e.WriteProfileInt(0x42, Signed(Word(m, 0xc)));
    return e.WriteProfileFont(0x86, m + 0x10);
}
// USER. A NULL LOGFONT keeps the current object and reports success.
inline bool SetIconTitleFont(Environment &e, const unsigned char *logfont, bool update_ini)
{
    // The original constructs the object before it looks at the pointer at all.
    const U font = e.CreateIconTitleFont(logfont);
    if (font)
    {
        if (e.IconTitleFont())
            e.DeleteFont(e.IconTitleFont());
        e.SetIconTitleFont(font);
    }
    if (!update_ini)
        return false;
    if (!logfont)
        return true;
    unsigned char stored[50] = {};
    e.FontObject(e.IconTitleFont(), stored);
    return e.WriteProfileFont(0x86, stored);
}
} // namespace Detail
// Complete SPI_SETWORKAREA branch USER.
inline bool SetWorkArea(Environment &e, const Parameter &p, U uiParam, bool &changed)
{
    Rect monitor, request;
    e.MonitorRect(monitor);
    if (p.null)
        request = monitor; // The original retargets lpvParam at the monitor rectangle.
    else if (p.data && p.capacity >= 8)
        // The internal service works on the RECT16 wire format, as the read branch does.
        request = {Detail::Signed(Detail::Word(p.data, 0)), Detail::Signed(Detail::Word(p.data, 2)),
                   Detail::Signed(Detail::Word(p.data, 4)), Detail::Signed(Detail::Word(p.data, 6))};
    else
        return false;
    Rect saved;
    e.WorkArea(saved); // CopyRect of the previous work area.
    Rect clipped;
    if (!IntersectRect(clipped, request, monitor))
    {
        e.SetWorkArea(saved);
        return false;
    }
    e.SetWorkArea(clipped);
    if (!EqualRect(clipped, request))
    {
        e.SetWorkArea(saved); // The original restores from its saved copy and fails.
        return false;
    }
    if (EqualRect(saved, clipped))
        return true; // Unchanged: TRUE, but no recompute, reposition or broadcast.
    e.RecomputeMetrics();
    if (uiParam)
        e.RepositionWindows(saved);
    changed = true;
    return true;
}
// Complete SPI_SETDESKWALLPAPER branch USER.
inline bool SetDeskWallpaper(Environment &e, const Parameter &p, bool update_ini, bool &changed)
{
    if (e.AppVersion() < 0x400 && update_ini && p.sentinel != 0xffffffffu && p.sentinel != 0xfffffffeu)
        e.RepaintDesktopSetting(0, 1);
    if (!e.SetDeskWallpaper(p))
        return false;
    if (!update_ini || p.sentinel == 0xffffffffu)
    {
        e.RepaintDesktop();
        return true;
    }
    char section[41] = {};
    const char *value = p.text;
    if (p.null)
    {
        e.SectionString(0x52, section, sizeof section);
        value = section;
    }
    changed = e.WriteProfileText(0x43, value);
    e.RepaintDesktop();
    return true;
}
// Complete SPI_SETDESKPATTERN branch USER.
inline bool SetDeskPattern(Environment &e, const Parameter &p, U uiParam, bool update_ini, bool &changed)
{
    const U pattern = uiParam & 0xffff;
    const bool defaulted = pattern == 0xffff;
    if (defaulted && !p.null)
        return false;
    if (e.AppVersion() < 0x400 && defaulted && update_ini)
        e.RepaintDesktopSetting(2, 2);
    if (!e.SetDeskPattern(p))
        return false;
    if (!update_ini)
        return true; // The original leaves its changed flag clear and broadcasts nothing.
    changed = e.WriteProfileText(0x02, p.text);
    return true;
}
// Complete dispatch of the ported write actions, including the shared exit.
// Returns -1 for an action outside this port, 0 for the original failure exit
// and 1 for the original success exit.
inline int Write(Environment &e, U action, U uiParam, const Parameter &p, U flags)
{
    if (action >> 16 || action - 1 > 0x71)
        return -1;
    const bool update_ini = (flags & 1) != 0, send_change = (flags & 2) != 0;
    bool changed = false;
    bool ok = true;
    U section = 0; // Resource identifier of the broadcast section name; zero loads nothing.
    switch (action)
    {
    case 20:
        ok = SetDeskWallpaper(e, p, update_ini, changed);
        section = 0x50;
        break;
    case 21:
        ok = SetDeskPattern(e, p, uiParam, update_ini, changed);
        section = 0x50;
        break;
    case 34: // SPI_SETICONTITLELOGFONT: NULL resets, otherwise uiParam must be sizeof LOGFONT16.
        if (p.null ? uiParam != 0 : (uiParam & 0xffff) != 0x32)
            return 0;
        changed = Detail::SetIconTitleFont(e, p.null ? nullptr : p.data, update_ini);
        section = 0x50;
        break;
    case 42:
    case 44:
    case 46:
    {
        // USER validates the 32-bit cbSize before it reaches.
        const U expected = action == 42 ? 0x154 : action == 44 ? 0x14 : 0x4c;
        if (p.null || !p.data || p.capacity < expected || Detail::Read32(p.data, 0) != expected)
            return 0;
        changed = action == 42   ? Detail::SetNonClientMetrics(e, p.data, update_ini)
                  : action == 44 ? Detail::SetMinimizedMetrics(e, p.data, update_ini)
                                 : Detail::SetIconMetrics(e, p.data, update_ini);
        break;
    }
    case 47:
        ok = SetWorkArea(e, p, uiParam, changed);
        break;
    default:
        return -1;
    }
    if (!ok)
        return 0;
    if (changed && send_change)
    {
        // The original loads the section name unconditionally; identifier zero
        // produces the empty string that branches without a section pass on.
        char name[41] = {};
        e.SectionString(section, name, sizeof name);
        e.Broadcast(action, name);
    }
    return 1;
}
} // namespace Win98ParametersWrite
