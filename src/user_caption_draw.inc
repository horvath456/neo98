// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside USER's anonymous provider namespace, after Fill/Edge/At.
// The caption bar, its buttons and the menu bar items are drawn by the
// same routines as on Windows 98.
// The DrawCaption body draws through the project's GDI. Colors,
// brushes, fonts, glyph rasters and the icon bitmap are the named providers;
// the active and inactive color choice, the icon slot, the vertical centering
// and the end-ellipsis decision follow Windows 98.
// USER's own default caption icon, the Windows logo it keeps as GROUP_ICON 105
// (resource 69h, at the small icon size). DrawCaption
// asks the window and its class for an icon and falls back to that handle when the window shows an icon at all. It is not the sample window
// of IDI_APPLICATION, GROUP_ICON 100. The handle space of the native frame has no entry for a USER-owned
// icon, so this value names it.
const unsigned DefaultApplicationIcon = 0xfffe0064u;

// USER, asked by DrawCaption and by the caption hit test
// before either reserves the icon cell. A WS_EX_TOOLWINDOW never has
// one. A child window or a window with WS_EX_DLGMODALFRAME, which every dialog
// with DS_MODALFRAME and so every property sheet is, has one only when it was
// given one: the small (atom [38Ah]) or large (atom [38Ch])
// icon of WM_SETICON, or a class icon; USER's default icon
// ([230h], and [0B4Ch] for the large one) does not count. A dialog of the
// class #32770 from a module before Windows 4.0 never has one.
// Every other window has one.
bool DefaultIcon(HWND h, unsigned icon)
{
    if (icon == DefaultApplicationIcon)
        return true;
    return default_icon_query && default_icon_query(CallbackContext(default_icon_context, h), icon);
}
bool CaptionIconEligible(HWND h, const Window &w)
{
    if (w.extended & 0x80)
        return false;
    if ((w.style & 0xc0000000u) != 0x40000000u && !(w.extended & 1))
        return true;
    if (!ModernWindow(h))
    {
        char name[16] = {};
        if (GetClassName(h, name, sizeof name) && !strcmp(name, "#32770"))
            return false;
    }
    if (w.icon_small)
        return !DefaultIcon(h, w.icon_small);
    if (w.icon_large && !DefaultIcon(h, w.icon_large))
        return true;
    const unsigned icon = ClassAccess(h, -14, 0, 2);   // GCL_HICON, class word +16h
    return icon && !DefaultIcon(h, icon);
}

struct CaptionEnvironment
{
    using U = unsigned;
    HWND window = 0; // the window being painted; its process resolves icon handles
    Gdi::HDC dc = 0;
    const unsigned *colors = nullptr;
    static constexpr U Dither = 0x1000;
    bool ok = true;
    static U Native(U rgb) { return Gdi::RGB(rgb >> 16, rgb >> 8, rgb); }
    static U Project(U native)
    { return ((native & 255) << 16) | (native & 0xff00) | ((native >> 16) & 255); }
    void Solid(Gdi::RECT r, unsigned rgb)
    {
        if (r.right <= r.left || r.bottom <= r.top)
            return;
        auto brush = Gdi::CreateSolidBrush(Gdi::RGB(rgb >> 16, rgb >> 8, rgb));
        if (!brush) { ok = false; return; }
        if (!Gdi::FillRect(dc, &r, brush)) ok = false;
        Gdi::DeleteObject(brush);
    }
    void Limit() { ok = false; }
    bool ValidWindow(U) { return true; }
    bool SystemMenu(U) { return true; }
    bool EligibleIcon(U) { return true; }
    U SmallIcon(U, bool) { return 0; }
    void Caption(U, char *out, U capacity, bool) { if (capacity) out[0] = 0; }
    U BkMode(U, U mode) { return Gdi::SetBkMode(dc, mode); }
    U TextColor(U, U rgb) { return Project(Gdi::SetTextColor(dc, Native(rgb))); }
    U BkColor(U, U rgb) { return Project(Gdi::SetBkColor(dc, Native(rgb))); }
    U Select(U, U font) { return Gdi::SelectObject(dc, font); }
    int Height(U)
    { Gdi::TEXTMETRICA t{}; if (!Gdi::GetTextMetricsA(dc, &t)) ok = false; return t.tmHeight; }
    U Length(const char *s) { U n = 0; while (s && s[n]) ++n; return n; }
    int Extent(const char *s, U n)
    { Gdi::SIZE z{}; if (!Gdi::GetTextExtentPoint32A(dc, s, (int)n, &z)) ok = false; return z.cx; }
    bool CachedIcon(U, Win98Caption::Rect, U, U) { return false; }
    bool CachedGradient(U, Win98Caption::Rect, U) { return false; }
    void Fill(U, Win98Caption::Rect r, U brush)
    {
        if (brush == Dither)
        {
            for (int y = r.top; y < r.bottom; ++y)
                for (int x = r.left; x < r.right; ++x)
                    Solid({x, y, x + 1, y + 1},
                          colors[(x + y) & 1 ? Win98Frame::ColorBtnHighlight
                                             : Win98Frame::ColorBtnFace]);
            return;
        }
        Solid({r.left, r.top, r.right, r.bottom},
              colors[brush < Win98Frame::ColorCount ? brush : 0]);
    }
    void Stripe(U, Win98Caption::Rect r, U rgb)
    { Solid({r.left, r.top, r.right, r.bottom}, rgb); }
    void DrawIcon(U, int x, int y, U icon, int width, int height);
    void DrawDefaultIcon(int x, int y, int width, int height);
    void Output(U, int x, int y, Win98Caption::Rect r, const char *s, U n)
    {
        if (!s || !n || r.right <= r.left)
            return;
        Gdi::TextOutA(dc, x, y, s, (int)n);
    }
    void Ellipsis(U, const char *s, U n, Win98Caption::Rect r, U, int margin)
    {
        char buffer[260] = {};
        U length = n < sizeof buffer - 5 ? n : (U)(sizeof buffer - 5);
        for (U i = 0; i < length; ++i) buffer[i] = s[i];
        buffer[length] = 0;
        Win98Caption::EndEllipsis(*this, buffer, length, sizeof buffer,
                                  r.right - r.left - margin, 0);
        Gdi::TEXTMETRICA t{};
        const int height = Gdi::GetTextMetricsA(dc, &t) ? t.tmHeight : 0;
        Output(0, r.left + margin, Win98Caption::Half(r.top + r.bottom - height), r,
               buffer, length);
    }
};
void CaptionEnvironment::DrawIcon(U, int x, int y, U icon, int width, int height)
{
    if (!icon)
        return;
    if (icon == DefaultApplicationIcon)
    {
        DrawDefaultIcon(x, y, width, height);
        return;
    }
    if (!frame_icon)
        return;
    // The icon handle belongs to the window's process, not to whichever
    // process registered the callback last (that one may already be gone).
    if (!frame_icon(CallbackContext(frame_icon_context, window), dc, x, y, icon, width, height))
        ok = false;
}

// The imported resource, scaled into the slot the caption metrics give it. The
// 32 pixel member serves anything above the small icon size, the 16 pixel one
// the usual SM_CXSMICON caption.
void CaptionEnvironment::DrawDefaultIcon(int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;
    const bool large = width > Win98UserResources::DefaultIcon16Width
                    || height > Win98UserResources::DefaultIcon16Height;
    const unsigned *pixels = large ? Win98UserResources::DefaultIcon32
                                   : Win98UserResources::DefaultIcon16;
    const int side = large ? Win98UserResources::DefaultIcon32Width
                           : Win98UserResources::DefaultIcon16Width;
    for (int row = 0; row < height; ++row)
    {
        const unsigned *source = pixels + (row * side / height) * side;
        int column = 0;
        while (column < width)
        {
            const unsigned value = source[column * side / width];
            int run = column + 1;
            while (run < width && source[run * side / width] == value)
                ++run;
            if (value >> 24)
                Solid({x + column, y + row, x + run, y + row + 1}, value & 0xffffffu);
            column = run;
        }
    }
}

// The icon USER offers the caption. The class small icon comes first, then the
// class icon, then USER's own default application icon. A window without
// WS_SYSMENU gets neither an icon nor the slot it would sit in.
unsigned SystemMenuIcon(HWND h, const Window &w)
{
    if (!(w.style & 0x00080000u) || !CaptionIconEligible(h, w))
        return 0;
    if (frame_icon)
    {
        // WM_SETICON's icons first (USER 6:xxxx asks WM_GETICON), small before large.
        if (w.icon_small) return w.icon_small;
        if (w.icon_large) return w.icon_large;
        unsigned icon = User::ClassAccess(h, -34, 0, 2);   // GCL_HICONSM
        if (!icon)
            icon = User::ClassAccess(h, -14, 0, 2);        // GCL_HICON
        if (!icon)
            icon = w.icon_drag;   // what WM_QUERYDRAGICON answered
        if (icon)
            return icon;
    }
    return DefaultApplicationIcon;
}

void PaintCaption(HWND h, const Window &w, Gdi::POINT p)
{
    const auto title = At(w.caption, p);
    const bool active = foreground == h;
    const unsigned *colors = Win98Frame::SystemColors();
    const int save = Gdi::SaveDC(screen);
    if (!save) { dirty = true; return; }
    if (!SystemClipRect(screen, title.left, title.top, title.right, title.bottom))
    { Gdi::RestoreDC(screen, save); return; }
    Gdi::SelectObject(screen, system_font);
    Win98Caption::State state;
    for (unsigned i = 0; i < Win98Frame::ColorCount; ++i)
    { state.colors[i] = colors[i]; state.brushes[i] = i; }
    state.dither = CaptionEnvironment::Dither;
    state.font = state.smallfont = system_font;
    Gdi::TEXTMETRICA metrics{};
    state.height = state.smallheight =
        Gdi::GetTextMetricsA(screen, &metrics) ? (int)metrics.tmHeight : 0;
    state.margin = settings.Word(0x338);        // SM_CXEDGE
    state.iconwidth = settings.Word(0x340);     // SM_CXSMICON
    state.iconheight = settings.Word(0x342);    // SM_CYSMICON
    state.border = settings.Word(0x2e8);        // SM_CXBORDER
    state.cacheheight = settings.Word(0x31a);   // SM_CXSIZE
    int display_width,display_height;
    User::ScreenMode(display_width,display_height,state.depth);
    // The caption text stops at the leftmost button.
    Win98Caption::Rect band{title.left, title.top, title.right, title.bottom};
    for (unsigned i = 0; i < w.button_slots; ++i)
    {
        const int left = w.button_slot[i].cell.left + p.x;
        if (left < band.right)
            band.right = left;
    }
    // The caption band behind and beside the buttons; DrawCaption then fills
    // only the part it writes text into. With gradient captions that part
    // ends in COLOR_GRADIENT(IN)ACTIVECAPTION, and Win98 shows the buttons on
    // that colour, not on the gradient's dark start.
    const bool gradient = settings.gradient_captions && state.depth > 8;
    Fill(title, colors[gradient ? (active ? 27 : 28) : (active ? 2 : 3)]);
    Win98Caption::Args args;
    args.window = h;
    args.dc = screen;
    args.text = w.title;
    args.icon = SystemMenuIcon(h, w);
    args.flags = 8u | 0x2000u | (gradient ? 0x20u : 0u) | (active ? 1u : 0u)
               | ((w.extended & 0x80) ? 2u : 0u) | (args.icon ? 4u : 0u);
    CaptionEnvironment e;
    e.window = h;
    e.dc = screen;
    e.colors = colors;
    Win98Caption::Draw(e, state, args, band);
    // The system menu is the one item of a menu bar of its own, and while the
    // menu mode of the bar has it selected, MNInvertItem draws
    // that item: places it over the icon cell of the caption, and the
    // selected item of a bar paints on COLOR_HIGHLIGHT with the
    // system bitmap, the small icon, on top.
    if (args.icon && (args.flags & 4) && User::SystemIconSelected(h))
    {
        const int height = title.bottom - title.top;
        e.Fill(screen, {title.left, title.top, title.left + height, title.bottom}, Win98MenuDraw::ColorHighlight);
        e.DrawIcon(screen, title.left + Win98Caption::Half(height - state.iconwidth) + state.border,
                   title.top + Win98Caption::Half(height - state.iconheight), args.icon, state.iconwidth,
                   state.iconheight);
    }
    Gdi::RestoreDC(screen, -1);
    for (unsigned i = 0; i < w.button_slots; ++i)
    {
        const auto &slot = w.button_slot[i];
        Gdi::RECT box = At(slot.cell, p);
        unsigned drawn = slot.state;
        if (w.pressed_hit == slot.hit && w.pressed_inside)
            drawn |= Win98Frame::Pushed;
        Win98Frame::DrawFrameControl(screen, &box, Win98Frame::Caption, drawn);
    }
}

void PaintMenuBar(const Window &w, Gdi::POINT p)
{
    const auto *menu = FindMenu(w.menu);
    if (!menu)
        return;
    const unsigned *colors = Win98Frame::SystemColors();
    Fill(At(w.menu_bar, p), colors[Win98MenuDraw::ColorMenu]);
    Gdi::SelectObject(screen, system_font);
    Win98MenuDraw::Settings menu_settings;
    menu_settings.colors = colors;
    menu_settings.marker_x = settings.Word(0x366);
    menu_settings.marker_y = settings.Word(0x368);
    Win98MenuDraw::Menu record;
    record.flags4 = 1;                     // this is a menu bar
    for (unsigned i = 0; i < w.menu_items && i < menu->count; ++i)
    {
        const auto box = At(w.menu_item[i], p);
        const int save = Gdi::SaveDC(screen);
        if (!save) { dirty = true; return; }
        if (!SystemClipRect(screen, box.left, box.top, box.right, box.bottom))
        { Gdi::RestoreDC(screen, save); return; }
        Win98MenuDraw::Item entry;
        // The original reads MFS_GRAYED and MFS_HILITE out of the item's third
        // byte; MF_DISABLED alone leaves the item drawn in its normal color.
        entry.state = MenuFlags(menu->items[i]) & 0x81;
        entry.x = box.left;
        entry.y = box.top;
        entry.cx = box.right - box.left;
        entry.cy = box.bottom - box.top;
        const auto choice = Win98MenuDraw::ItemColors(menu_settings, entry, record, 0);
        if (choice.fill)
            Fill(box, colors[choice.background]);
        if(menu->items[i].type&0x100){
            PaintOwnerBarItem(w,p,box);
            Gdi::RestoreDC(screen,-1);
            continue;
        }
        if(menu->items[i].bitmap==0xffff)PaintOwnerBarItem(w,p,box);
        // USER: a menu bar item skips a leading 08h, the old
        // right-alignment mark, and draws the rest.
        const char *source = MenuText(menu->items[i]);
        if (*source == '\b')
            ++source;
        // hands its renderer a DrawState style, and USER obeys it.
        // DSS_DISABLED, 20h, and the whole-menu gray of 100h are the etched
        // pair: the string once in COLOR_3DHILIGHT one pixel down and right,
        // then once in COLOR_3DSHADOW. DSS_UNION, 10h, is the dithered gray a
        // scheme whose menu, shadow and highlight are one colour falls back to.
        // The text keeps its '&': the letter after it is underlined.
        const unsigned count = Length(source);
        if (choice.flags & 0x120)
        {
            PrefixLabel(screen, box.left + 9, box.top + 3, source, count, colors[Win98MenuDraw::ColorBtnHighlight]);
            PrefixLabel(screen, box.left + 8, box.top + 2, source, count, colors[Win98MenuDraw::ColorBtnShadow]);
        }
        else if (choice.flags & 0x10)
            PrefixLabel(screen, box.left + 8, box.top + 2, source, count, colors[Win98MenuDraw::ColorGrayText]);
        else
            PrefixLabel(screen, box.left + 8, box.top + 2, source, count, colors[choice.text]);
        Gdi::RestoreDC(screen, -1);
    }
}

// DrawMenuBarTemp, USER over : the menu is laid out again for
// the width of the caller's rectangle, the strip is filled with the
// menu colour and the top-level items are drawn into the
// caller's DC, with the caller's font when there is one. The answer is the
// height the menu had remembered before. The layout measures with USER's own
// menu font; the original measures with the swapped metrics of the font
// passed in.
int DrawMenuBarTempBody(HWND owner, Gdi::HDC dc, Gdi::RECT rect, HMENU handle, Gdi::HFONT font)
{
    auto *menu = FindMenu(handle);
    if (!menu)
        return 0;
    const int previous = menu->height;
    int width = 0, height = 0;
    if (!MeasureMenuObject(handle, owner, rect.right - rect.left, width, height))
        return 0;
    menu = FindMenu(handle);
    if (!menu)
        return 0;
    const unsigned *colors = Win98Frame::SystemColors();
    Fill(dc, rect, colors[Win98MenuDraw::ColorMenu]);
    const int save = Gdi::SaveDC(dc);
    Gdi::SelectObject(dc, font ? font : system_font);
    Win98MenuDraw::Settings menu_settings;
    menu_settings.colors = colors;
    menu_settings.marker_x = settings.Word(0x366);
    menu_settings.marker_y = settings.Word(0x368);
    Win98MenuDraw::Menu record;
    record.flags4 = 1;
    for (unsigned i = 0; i < menu->count && i < 48; ++i)
    {
        const auto &item = menu->items[i];
        Gdi::RECT box{rect.left + item.x, rect.top + item.y, rect.left + item.x + item.width,
                      rect.top + item.y + item.height};
        Win98MenuDraw::Item entry;
        entry.state = MenuFlags(item) & 0x81;
        entry.x = box.left;
        entry.y = box.top;
        entry.cx = box.right - box.left;
        entry.cy = box.bottom - box.top;
        const auto choice = Win98MenuDraw::ItemColors(menu_settings, entry, record, 0);
        if (choice.fill)
            Fill(dc, box, colors[choice.background]);
        const char *source = MenuText(item);
        if (*source == '\b')
            ++source;
        const unsigned count = Length(source);
        if (choice.flags & 0x120)
        {
            PrefixLabel(dc, box.left + 9, box.top + 3, source, count, colors[Win98MenuDraw::ColorBtnHighlight]);
            PrefixLabel(dc, box.left + 8, box.top + 2, source, count, colors[Win98MenuDraw::ColorBtnShadow]);
        }
        else if (choice.flags & 0x10)
            PrefixLabel(dc, box.left + 8, box.top + 2, source, count, colors[Win98MenuDraw::ColorGrayText]);
        else
            PrefixLabel(dc, box.left + 8, box.top + 2, source, count, colors[choice.text]);
    }
    if (save)
        Gdi::RestoreDC(dc, save);
    return previous;
}

