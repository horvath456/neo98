// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 USER nonclient geometry. Native fields replace segmented USER records.
#pragma once
namespace Win98Nonclient
{
using U = unsigned;
struct Rect
{
    int left, top, right, bottom;
};
struct WindowState
{
    U style, extended, flags, menu;
};
struct Environment
{
    virtual unsigned short Word(U offset) const = 0;
    virtual unsigned short ExpectedVersion() const = 0;
    virtual WindowState ReadWindow(U window) const = 0;
    virtual void WriteWindow(U window, const WindowState &) = 0;
    virtual bool MenuValid(U menu) = 0;
    virtual int MenuHeight(U menu, U window, int available_width, int border_width, int top_offset) = 0;
    virtual void NotifyReorder(U window) = 0;
};
inline int Signed(U value) { return (value & 0x8000) ? int(value & 0xffff) - 65536 : int(value & 0xffff); }
inline int Add(int a, int b) { return Signed(U(a) + U(b)); }
inline int Sub(int a, int b) { return Signed(U(a) - U(b)); }
inline Rect Narrow(const Rect &r)
{
    return {Signed(r.left), Signed(r.top), Signed(r.right), Signed(r.bottom)};
}
inline void Inflate(Rect &r, int x, int y)
{
    r.left = Sub(r.left, x);
    r.top = Sub(r.top, y);
    r.right = Add(r.right, x);
    r.bottom = Add(r.bottom, y);
}
// Complete USER. Arguments and the returned count have WORD width.
inline U BorderUnits(const Environment &e, U style, U extended, U outer, U client_edge)
{
    U count = 0;
    if (outer & 0xffff)
    {
        if (extended & 0x100)
            count = 2;
        else if (extended & 0x20000)
            count = 1;
        if ((style & 0xc00000) || (extended & 1))
            ++count;
        if (style & 0x40000)
            count += e.Word(0x1014);
    }
    if ((client_edge & 0xffff) && (extended & 0x200))
        count += 2;
    return count & 0xffff;
}
// Complete USER. The caller converts the task version to 0 or 1.
inline U HasWindowEdge(U style, U extended, U modern)
{
    if (extended & 1)
        return 1;
    if (extended & 0x20000)
        return 0;
    if (style & 0x40000)
        return 1;
    const U kind = style & 0xc00000;
    if (kind == 0x400000)
        return 1;
    return kind == 0xc00000 ? modern & 0xffff : 0;
}
// Complete USER. Public pointer validation belongs to the adapter.
inline U Adjust(Environment &e, Rect &rectangle, U style, U has_menu, U extended)
{
    rectangle = Narrow(rectangle);
    if (HasWindowEdge(style, extended, e.ExpectedVersion() >= 0x400))
        extended |= 0x100;
    else
        extended &= ~0x100u;
    if (has_menu & 0xffff)
        rectangle.top = Sub(rectangle.top, e.Word(0x2fc));
    if ((style & 0xc00000) == 0xc00000)
        rectangle.top = Sub(rectangle.top, e.Word(extended & 0x80 ? 0x344 : 0x2e6));
    const U border = BorderUnits(e, style, extended, 1, 1);
    Inflate(rectangle, Signed(border * e.Word(0x2e8)), Signed(border * e.Word(0x2ea)));
    return 1;
}
// Complete USER. mode==0 subtracts menu/scrollbar space; other WORD
// modes still detect those elements but retain their area in the returned rect.
inline void Calculate(Environment &e, U window, Rect &rectangle, U mode)
{
    rectangle = Narrow(rectangle);
    const U original_flags = e.ReadWindow(window).flags & 15;
    auto w = e.ReadWindow(window);
    w.flags &= ~(15u | 0x00800000u);
    e.WriteWindow(window, w);
    if (w.style & 0x20000000)
    {
        rectangle.right = rectangle.left;
        rectangle.bottom = rectangle.top;
    }
    else
    {
        Rect r = rectangle;
        const int original_top = r.top;
        if ((w.style & 0xc00000) == 0xc00000)
        {
            w.flags |= 8;
            e.WriteWindow(window, w);
            r.top = Add(r.top, e.Word(w.extended & 0x80 ? 0x344 : 0x2e6));
        }
        w = e.ReadWindow(window);
        const U border = BorderUnits(e, w.style, w.extended, 1, 0);
        const int border_x = Signed(border * e.Word(0x2e8));
        const int border_y = Signed(border * e.Word(0x2ea));
        Inflate(r, Signed(0u - U(border_x)), Signed(0u - U(border_y)));
        w = e.ReadWindow(window);
        if ((w.style & 0xc0000000) != 0x40000000 && e.MenuValid(w.menu))
        {
            w = e.ReadWindow(window);
            w.flags |= 1;
            e.WriteWindow(window, w);
            if (!(mode & 0xffff))
                r.top = Add(r.top, e.MenuHeight(w.menu, window, Sub(r.right, r.left), border_x,
                                                Sub(r.top, original_top)));
        }
        bool empty = false;
        if (r.top >= r.bottom)
        {
            r.bottom = r.top;
            empty = true;
        }
        w = e.ReadWindow(window);
        if ((w.flags & 0x00400000) && r.right <= r.left)
        {
            r.right = r.left;
            empty = true;
        }
        if (!empty)
        {
            if ((w.extended & 0x200) && Sub(r.right, r.left) >= Signed(e.Word(0x6e8)) &&
                Sub(r.bottom, r.top) >= Signed(e.Word(0x6ea)))
            {
                w.flags |= 0x00800000;
                e.WriteWindow(window, w);
                Inflate(r, Signed(0u - e.Word(0x338)), Signed(0u - e.Word(0x33a)));
            }
            w = e.ReadWindow(window);
            if ((w.style & 0x100000) && Sub(r.bottom, r.top) > Signed(e.Word(0x2e4)))
            {
                w.flags |= 4;
                e.WriteWindow(window, w);
                if (!(mode & 0xffff))
                    r.bottom = Sub(r.bottom, e.Word(0x2e4));
            }
            w = e.ReadWindow(window);
            if ((w.style & 0x200000) && Sub(r.right, r.left) >= Signed(e.Word(0x2e2)))
            {
                w.flags |= 2;
                e.WriteWindow(window, w);
                if (!(mode & 0xffff))
                    r.right = Sub(r.right, e.Word(0x2e2));
            }
        }
        rectangle = r;
    }
    if (e.Word(0x2ae) && (e.ReadWindow(window).flags & 15) != original_flags)
        e.NotifyReorder(window);
}
} // namespace Win98Nonclient
