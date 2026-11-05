// SPDX-License-Identifier: GPL-2.0-or-later
// ChooseColor of the 16-bit COMMDLG (ordinal 5), built natively the way
// COMMDLG.DLL of Win98 builds it. Included in src/win16_commdlg.cpp.
//
// The 16-bit COMMDLG shows its own colour dialog in the style of Windows
// 3.1, all of it in segment 6: ChooseColor checks the structure,
// takes the template (CC_ENABLETEMPLATEHANDLE, CC_ENABLETEMPLATE or its own
// "ChooseColor", string 303h) and runs DialogBoxIndirectParam with the dialog
// procedure. The procedure handles:
//
// WM_INITDIALOG: the rectangles, the colour cells
// (48 basic colours, 16 custom ones from the
// caller), CC_RGBINIT, CC_FULLOPEN (which paints the spectrum into
// a bitmap) or the dialog cut down to its left half, the hook last
// WM_COMMAND: OK and Cancel with commdlg_ColorOK, the six edit
// fields (EN_CHANGE, and EN_KILLFOCUS for red, green and blue),
// "Add to Custom Colors", "Define Custom Colors >>", the hidden
// solid colour button, Help with commdlg_help to the owner
// WM_PAINT : cells, focus, the two halves of the
// current colour, the luminosity bar, its arrow, the spectrum and
// its cross
// mouse input on the spectrum, the luminosity bar and the cells,
// with capture and ClipCursor, released afterwards; a double
// click on the solid half takes the nearest solid colour
// arrow keys, Home and End on the two cell boxes, the
// space bar selects through the subclass of the two boxes
// commdlg_SetRGBColor from the hook
//
// RGB-to-HLS and HLS-to-RGB conversions use integer arithmetic, with 240 steps for hue, saturation and luminosity and
// the word arithmetic of the original. The hook is the caller's 16-bit
// procedure; it sees every message first, WM_INITDIALOG with the far pointer
// of its own CHOOSECOLOR, and commdlg_ColorOK with the same pointer.
namespace
{
constexpr unsigned Win16ColorDialogToken = 0x71000045, Win16ColorBoxToken = 0x71000046;
// CHOOSECOLOR16.
constexpr unsigned CcOwner = 0x04, CcInstance = 0x06, CcResult = 0x08, CcCustom = 0x0c, CcFlags = 0x10,
                   CcHook = 0x18, CcTemplate = 0x1c;
constexpr unsigned CcRgbInit = 0x1, CcFullOpen = 0x2, CcPreventFullOpen = 0x4, CcShowHelp = 0x8,
                   CcEnableHook = 0x10, CcEnableTemplate = 0x20, CcEnableTemplateHandle = 0x40,
                   CcSolidColor = 0x80, CcOldApplication = 0x10000000;
// Control identifiers of the template (colordlg.h).
constexpr unsigned ColorHue = 0x2bf, ColorSat = 0x2c0, ColorLum = 0x2c1, ColorRed = 0x2c2, ColorGreen = 0x2c3,
                   ColorBlue = 0x2c4, ColorCurrent = 0x2c5, ColorRainbow = 0x2c6, ColorLumScroll = 0x2be,
                   ColorAdd = 0x2c8, ColorSolid = 0x2c9, ColorMix = 0x2cf, ColorBox = 0x2d0, ColorCustom = 0x2d1,
                   ColorHueAccel = 0x2d3, ColorBlueAccel = 0x2d8, ColorSolidRight = 0x2db, PushHelp = 0x40e;
constexpr unsigned BasicColors = 48;
// the 48 basic colours, COLORREF.
const unsigned BasicTable[BasicColors] = {
    0x8080ff, 0x80ffff, 0x80ff80, 0x80ff00, 0xffff80, 0xff8000, 0xc080ff, 0xff80ff, 0x0000ff, 0x00ffff,
    0x00ff80, 0x40ff00, 0xffff00, 0xc08000, 0xc08080, 0xff00ff, 0x404080, 0x4080ff, 0x00ff00, 0x808000,
    0x804000, 0xff8080, 0x400080, 0x8000ff, 0x000080, 0x0080ff, 0x008000, 0x408000, 0xff0000, 0xa00000,
    0x800080, 0xff0080, 0x000040, 0x004080, 0x004000, 0x404000, 0x800000, 0x400000, 0x400040, 0x800040,
    0x000000, 0x008080, 0x408080, 0x808080, 0x808040, 0xc0c0c0, 0x400040, 0xffffff};
// the help identifiers of HELP_WM_HELP and HELP_CONTEXTMENU.
const unsigned HelpTable[] = {0x2d0, 0x6f74, 0x2d1, 0x6f75, 0x2cf, 0x6f5c, 0x2c6, 0x6f52, 0x2be, 0x6f76, 0x2c5,
                              0x6f5b, 0x2c9, 0x6f5b, 0x2da, 0x6f5b, 0x2db, 0x6f5b, 0x2d3, 0x6f54, 0x2bf, 0x6f54,
                              0x2d4, 0x6f55, 0x2c0, 0x6f55, 0x2d5, 0x6f59, 0x2c1, 0x6f59, 0x2d6, 0x6f56, 0x2c2,
                              0x6f56, 0x2d7, 0x6f57, 0x2c3, 0x6f57, 0x2d8, 0x6f58, 0x2c4, 0x6f58, 0x2c8, 0x6f5a,
                              0, 0};
// A resource of an NE image by name: the identifier word without the 8000h
// bit is the offset of a counted name from the start of the resource table.
const unsigned char *NeNamedResource(const unsigned char *d, unsigned length, unsigned type, const char *name,
                                     unsigned &size)
{
    auto u16 = [&](unsigned at) { return at + 2 <= length ? unsigned(d[at] | d[at + 1] << 8) : 0u; };
    if (length < 0x40)
        return nullptr;
    const unsigned ne = u16(0x3c) | u16(0x3e) << 16;
    if (ne + 0x40 > length || d[ne] != 'N' || d[ne + 1] != 'E')
        return nullptr;
    const unsigned table = ne + u16(ne + 0x24);
    unsigned at = table;
    const unsigned shift = u16(at);
    at += 2;
    auto same = [&](unsigned label) {
        if (label >= length)
            return false;
        const unsigned n = d[label];
        unsigned i = 0;
        for (; i < n && name[i] && label + 1 + i < length; ++i)
        {
            char a = char(d[label + 1 + i]), b = name[i];
            if (a >= 'a' && a <= 'z')
                a = char(a - 32);
            if (b >= 'a' && b <= 'z')
                b = char(b - 32);
            if (a != b)
                return false;
        }
        return i == n && !name[i];
    };
    for (unsigned guard = 0; guard < 64 && at + 8 <= length; ++guard)
    {
        const unsigned kind = u16(at), count = u16(at + 2);
        if (!kind)
            break;
        at += 8;
        for (unsigned i = 0; i < count && at + 12 <= length; ++i, at += 12)
        {
            const unsigned id = u16(at + 6);
            if (kind == (0x8000 | type) && !(id & 0x8000) && same(table + id))
            {
                const unsigned offset = u16(at) << shift;
                size = u16(at + 2) << shift;
                if (offset >= length)
                    return nullptr;
                if (offset + size > length)
                    size = length - offset;
                return d + offset;
            }
        }
    }
    return nullptr;
}
// String `id` of an NE image's string table.
bool NeString(const unsigned char *d, unsigned length, unsigned id, char *out, unsigned capacity)
{
    unsigned size = 0;
    const unsigned char *block = NeResource(d, length, 6, (id >> 4) + 1, size);
    if (!block || !capacity)
        return false;
    unsigned at = 0;
    for (unsigned i = 0; i < (id & 15) && at < size; ++i)
        at += 1u + block[at];
    if (at >= size)
        return false;
    unsigned n = block[at];
    if (n + 1 > capacity)
        n = capacity - 1;
    if (at + 1 + n > size)
        return false;
    for (unsigned i = 0; i < n; ++i)
        out[i] = char(block[at + 1 + i]);
    out[n] = 0;
    return n != 0;
}
struct Box
{
    int left = 0, top = 0, right = 0, bottom = 0;
};
// The unsigned 32-bit division of COMMDLG.
unsigned Divide(unsigned dividend, unsigned divisor) { return divisor ? dividend / divisor : 0; }
// GDI.128 MulDiv: the product over the divisor, rounded.
int MulDiv16(int number, int numerator, int denominator)
{
    if (!denominator)
        return -1;
    const long long product = (long long)number * numerator;
    long long result = product >= 0 ? (product + denominator / 2) / denominator
                                     : (product - denominator / 2) / denominator;
    if (denominator < 0)
        result = product >= 0 ? (product - denominator / 2) / denominator : (product + denominator / 2) / denominator;
    return int(short(result));
}
// one channel of HLS to RGB. The hue is a word; a negative one wraps
// and ends in the last branch, which is the answer of hue + 240 there.
unsigned HueToChannel(unsigned n1, unsigned n2, unsigned hue)
{
    hue &= 0xffff;
    if (hue >= 240)
        hue = (hue - 240) & 0xffff;
    if (hue < 40)
        return ((((n2 - n1) & 0xffff) * hue + 20) / 40 + n1) & 0xffff;
    if (hue < 120)
        return n2;
    if (hue < 160)
        return ((((n2 - n1) & 0xffff) * (160 - hue) + 20) / 40 + n1) & 0xffff;
    return n1;
}
// (hue, lum, sat) to a COLORREF.
unsigned HlsToRgb(unsigned hue, unsigned lum, unsigned sat)
{
    unsigned r, g, b;
    if (!sat)
        r = g = b = ((lum * 0xff) & 0xffff) / 240;
    else
    {
        unsigned magic2;
        if (lum <= 120)
            magic2 = Divide((sat + 240) * lum + 120, 240) & 0xffff;
        else
            magic2 = (sat - Divide((sat * lum) + 120, 240) + lum) & 0xffff;
        const unsigned magic1 = (lum + lum - magic2) & 0xffff;
        r = (((HueToChannel(magic1, magic2, hue + 80) * 0xff) & 0xffff) + 120) / 240;
        g = (((HueToChannel(magic1, magic2, hue) * 0xff) & 0xffff) + 120) / 240;
        b = (((HueToChannel(magic1, magic2, hue - 80) * 0xff) & 0xffff) + 120) / 240;
    }
    return (r & 0xff) | (g & 0xff) << 8 | (b & 0xff) << 16;
}
// a COLORREF to hue, saturation and luminosity.
void RgbToHls(unsigned rgb, int &hue, int &sat, int &lum)
{
    const unsigned red = rgb & 0xff, green = (rgb >> 8) & 0xff, blue = (rgb >> 16) & 0xff;
    unsigned high = green, low = green;
    if (high < red)
        high = red;
    if (high < blue)
        high = blue;
    if (low > red)
        low = red;
    if (low > blue)
        low = blue;
    const unsigned sum = high + low;
    lum = int(Divide(sum * 240 + 0xff, 510));
    const unsigned delta = high - low;
    if (!delta)
    {
        sat = 0;
        hue = 160;
        return;
    }
    if (unsigned(lum) <= 120)
        sat = int(Divide(delta * 240 + (sum >> 1), sum));
    else
        sat = int(Divide(delta * 240 + ((510 - sum) >> 1), 510 - sum));
    const unsigned half = delta >> 1;
    const int rdelta = int(Divide((high - red) * 40 + half, delta));
    const int gdelta = int(Divide((high - green) * 40 + half, delta));
    const int bdelta = int(Divide((high - blue) * 40 + half, delta));
    int h;
    if (high == red)
        h = bdelta - gdelta;
    else if (high == green)
        h = rdelta - bdelta + 80;
    else
        h = gdelta - rdelta + 160;
    h = int(short(h));
    if (h < 0)
        h += 240;
    if (unsigned(h) >= 240)
        h -= 240;
    hue = h;
}
struct Win16ColorFrame
{
    Application *app;
    Win16ColorFrame *next;
    unsigned hwnd = 0, native_template = 0;
    bool initializing = true;
    unsigned cc = 0, cc_far = 0, hook = 0;
    bool ready = false; // nothing but WM_INITDIALOG before the record exists
    unsigned rgb = 0;
    int hue = 0, sat = 0, lum = 0;
    int width = 0, height = 0, lum_height = 0;
    unsigned custom = 0x30, basic = 0, current = 0;
    int cross_x = 0, cross_y = 0, arrow_y = 0;
    Box dialog, rainbow, arrow, lum_bar, left_half, solid_half, sample;
    bool full = false;
    unsigned colors[64] = {};
    Box cells[64];
    int cell_width = 0, cell_height = 0;
    unsigned memory_dc = 0, bitmap = 0;
    unsigned message_ok = 0, message_rgb = 0, message_help = 0;
    unsigned scratch = 0; // guest bytes for rectangles and texts, 1024
    static bool capture; // one for all dialogs
    static unsigned box_procedure; // the procedure the two boxes had
    static Win16ColorFrame *head;
    Win16ColorFrame(Application *a) : app(a), next(head) { head = this; }
    ~Win16ColorFrame()
    {
        auto **at = &head;
        while (*at && *at != this)
            at = &(*at)->next;
        if (*at)
            *at = next;
    }
};
bool Win16ColorFrame::capture = false;
unsigned Win16ColorFrame::box_procedure = 0;
Win16ColorFrame *Win16ColorFrame::head = nullptr;
Win16ColorFrame *Win16ColorFrameOf(Application *app, unsigned hwnd, bool initialize)
{
    for (auto *f = Win16ColorFrame::head; f; f = f->next)
        if (f->app == app && (f->hwnd == hwnd || (initialize && !f->hwnd)))
        {
            if (!f->hwnd)
                f->hwnd = hwnd;
            return f;
        }
    return nullptr;
}
} // namespace

struct Application::Win16ColorDialog
{
    Application &app;
    Win16ColorFrame &f;
    bool recorded = false;
    template <class... T> unsigned Call(const char *name, T... values)
    {
        const unsigned a[] = {unsigned(values)..., 0u};
        return app.Api(name, a);
    }
    unsigned Item(unsigned id) { return User::GetDlgItem(f.hwnd, id); }
    unsigned Flags() { return app.Read32(f.cc + CcFlags); }
    // A rectangle in guest memory for the calls that take one.
    unsigned Rect(const Box &b, unsigned slot = 0)
    {
        const int v[4] = {b.left, b.top, b.right, b.bottom};
        app.Store(f.scratch + slot * 16, v, 16);
        return f.scratch + slot * 16;
    }
    Box ReadRect(unsigned at)
    {
        int v[4] = {};
        app.Read(at, v, 16);
        return Box{v[0], v[1], v[2], v[3]};
    }
    static bool Inside(const Box &b, int x, int y) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; }
    static bool Intersect(const Box &a, const Box &b, Box &out)
    {
        out = Box{a.left > b.left ? a.left : b.left, a.top > b.top ? a.top : b.top,
                  a.right < b.right ? a.right : b.right, a.bottom < b.bottom ? a.bottom : b.bottom};
        return out.left < out.right && out.top < out.bottom;
    }
    unsigned Dc() { return Call("GetDC", f.hwnd); }
    void Release(unsigned dc) { Call("ReleaseDC", f.hwnd, dc); }
    void Fill(unsigned dc, const Box &b, unsigned colour)
    {
        const unsigned brush = Call("CreateSolidBrush", colour);
        Call("FillRect", dc, Rect(b, 3), brush);
        Call("DeleteObject", brush);
    }
    void Invalidate(const Box &b) { Call("InvalidateRect", f.hwnd, Rect(b), 0); }
    void Validate(const Box &b) { Call("ValidateRect", f.hwnd, Rect(b)); }
    // with CC_SOLIDCOLOR the nearest colour of the screen.
    unsigned Nearest(unsigned rgb)
    {
        if (!(Flags() & CcSolidColor))
            return rgb;
        const unsigned dc = Call("GetDC", 0);
        rgb = Call("GetNearestColor", dc, rgb);
        Call("ReleaseDC", 0, dc);
        return rgb;
    }
    unsigned Hook(unsigned message, unsigned wp, unsigned lp)
    {
        if (!f.hook || !User::IsWindow(f.hwnd))
            return 0;
        const unsigned arguments[] = {f.hwnd, message, wp, lp};
        return app.Call(f.hook, arguments, 4) & 0xffff;
    }
    void Hourglass(bool on)
    {
        // the wait cursor while the spectrum is painted.
        const unsigned cursor = Call("LoadCursorA", 0, on ? 0x7f02u : 0x7f00u);
        Call("SetCursor", cursor);
    }
    // the rectangles of the spectrum, the luminosity bar with the
    // strip of its arrow, and the two halves of the current colour.
    void Rectangles()
    {
        auto client = [&](unsigned id, Box &b) {
            const unsigned item = Item(id);
            const unsigned at = f.scratch;
            Call("GetClientRect", item, at);
            Call("MapWindowPoints", item, f.hwnd, at, 2);
            b = ReadRect(at);
        };
        client(ColorRainbow, f.rainbow);
        client(ColorLumScroll, f.lum_bar);
        client(ColorCurrent, f.sample);
        f.arrow = f.lum_bar;
        f.arrow.left = f.lum_bar.right;
        const int size = int(Call("GetSystemMetrics", 0x1e));
        f.arrow.right = f.lum_bar.right + (size - (size >> 31)) / 2;
        f.lum_height = f.lum_bar.bottom - f.lum_bar.top;
        f.solid_half = f.sample;
        f.left_half = f.sample;
        const int middle = (f.sample.left + f.sample.right) / 2;
        f.left_half.right = middle;
        f.solid_half.left = middle;
    }
    // the cells of the two boxes and the basic colours; the boxes get
    // the subclass.
    void Cells()
    {
        const unsigned basic = Item(ColorBox), custom = Item(ColorCustom);
        const unsigned previous = User::Procedure(basic);
        if (previous != Win16ColorBoxToken)
            Win16ColorFrame::box_procedure = previous;
        User::SetProcedure(basic, Win16ColorBoxToken);
        User::SetProcedure(custom, Win16ColorBoxToken);
        auto area = [&](unsigned item) {
            const unsigned at = f.scratch;
            Call("GetWindowRect", item, at);
            Call("ScreenToClient", f.hwnd, at);
            Call("ScreenToClient", f.hwnd, at + 8);
            Box b = ReadRect(at);
            b.right -= 3;
            b.left += 3;
            b.bottom -= 3;
            b.top += 3;
            return b;
        };
        const Box first = area(basic);
        f.cell_width = (first.right - first.left) / 8;
        f.cell_height = (first.bottom - first.top) / 6;
        for (unsigned i = 0; i < 64; ++i)
        {
            const Box from = i < 48 ? first : area(custom);
            const unsigned n = i < 48 ? i : i - 48;
            Box &c = f.cells[i];
            c.left = int(n % 8) * f.cell_width + from.left;
            c.right = c.left + f.cell_width - 5;
            c.top = int(n / 8) * f.cell_height + from.top;
            c.bottom = c.top + f.cell_height - 5;
            if (i < BasicColors)
                f.colors[i] = BasicTable[i];
        }
    }
    static Box Grow(Box b, int by)
    {
        b.left -= by;
        b.top -= by;
        b.right += by;
        b.bottom += by;
        return b;
    }
    // the frame around the selected cell, black or in the face colour.
    void CellFrame(unsigned dc, unsigned index, bool on)
    {
        const Box b = Grow(f.cells[index & 63], 1);
        const unsigned colour = on ? 0u : Call("GetSysColor", 15);
        const unsigned brush = Call("CreateSolidBrush", colour);
        Call("FrameRect", dc, Rect(b, 2), brush);
        Call("DeleteObject", brush);
    }
    // the selection moves to a cell.
    void Select(unsigned index)
    {
        const unsigned dc = Dc();
        CellFrame(dc, f.current, false);
        CellFrame(dc, index, true);
        Release(dc);
        f.rgb = Nearest(f.colors[index & 63]);
    }
    // the focus rectangle moves inside one box.
    void Focus(unsigned index)
    {
        unsigned &slot = index >= 0x30 ? f.custom : f.basic;
        const unsigned face = Call("GetSysColor", 15);
        const unsigned pen = Call("CreatePen", 0, 1, face);
        const unsigned hollow = Call("GetStockObject", 5);
        const unsigned dc = Dc();
        const unsigned old_pen = Call("SelectObject", dc, pen);
        const unsigned old_brush = Call("SelectObject", dc, hollow);
        Box b = Grow(f.cells[slot & 63], 3);
        Call("Rectangle", dc, unsigned(b.left), unsigned(b.top), unsigned(b.right), unsigned(b.bottom));
        slot = index;
        b = Grow(f.cells[index & 63], 3);
        Call("Rectangle", dc, unsigned(b.left), unsigned(b.top), unsigned(b.right), unsigned(b.bottom));
        Call("DrawFocusRect", dc, Rect(b));
        Call("SelectObject", dc, old_pen);
        Call("SelectObject", dc, old_brush);
        Release(dc);
        Call("DeleteObject", pen);
    }
    // One cell, sunken, in its colour.
    void Cell(unsigned dc, unsigned index)
    {
        if (index < 0x30 && index >= BasicColors)
            return;
        const unsigned at = Rect(f.cells[index], 1);
        Call("DrawEdge", dc, at, 0xa, 0x200f);
        const unsigned brush = Call("CreateSolidBrush", Nearest(f.colors[index]));
        Call("FillRect", dc, at, brush);
        Call("DeleteObject", brush);
        if (f.current == index)
            CellFrame(dc, index, true);
    }
    // the cell an arrow key, Home or End leads to.
    bool Key(unsigned key, unsigned &out)
    {
        const unsigned focus = User::GetFocus();
        unsigned id = 0;
        User::WindowLong(focus, -12, id, false);
        unsigned cx;
        if (id == ColorBox)
            cx = f.basic;
        else if (id == ColorCustom)
            cx = f.custom;
        else
            return false;
        switch (key)
        {
        case 0x23: cx = f.basic == cx ? BasicColors - 1 : 0x3f; break;
        case 0x24: cx = f.basic == cx ? 0 : 0x30; break;
        case 0x25:
            if (cx & 7)
                --cx;
            break;
        case 0x26:
            if (cx >= 0x38 || (cx < 0x30 && cx >= 8))
                cx -= 8;
            break;
        case 0x27:
            ++cx;
            if (!(cx & 7))
                --cx;
            break;
        case 0x28:
            if (cx < 0x28 || (cx >= 0x30 && cx < 0x38))
                cx += 8;
            break;
        default: break;
        }
        if (!(BasicColors > cx || cx >= 0x30))
            cx = f.basic;
        out = cx;
        return f.basic != cx && f.custom != cx;
    }
    // the arrow beside the luminosity bar, a triangle to the left.
    void Arrow(unsigned dc, int y)
    {
        const unsigned brush = Call("GetSysColorBrush", 0x12);
        const unsigned old = Call("SelectObject", dc, brush);
        int h = 1;
        for (int x = f.arrow.left + 2; x < f.arrow.right - 2; ++x, h += 2)
            Call("PatBlt", dc, unsigned(x), unsigned(y - h / 2), 1, unsigned(h), 0xf00021);
        Call("SelectObject", dc, old);
    }
    // the arrow goes, in the brush of the dialog.
    void EraseArrow(unsigned dc)
    {
        const unsigned brush = app.Send(f.hwnd, 0x136, dc, f.hwnd);
        const Box b{f.arrow.left + 1, (f.arrow.left - f.arrow.right - 1) / 2 + f.arrow_y, f.arrow.right,
                    (f.arrow.right - f.arrow.left + 1) / 2 + f.arrow_y};
        Call("FillRect", dc, Rect(b), brush);
    }
    // the spectrum comes back under the cross.
    void EraseCross(unsigned dc)
    {
        const Box r = f.rainbow;
        const int cy = int(Call("GetSystemMetrics", 6)) * 10, cx = int(Call("GetSystemMetrics", 5)) * 10;
        unsigned top = unsigned(f.cross_y - cy), bottom = unsigned(f.cross_y + cy);
        if (top < unsigned(r.top))
            top = unsigned(r.top);
        if (bottom > unsigned(r.bottom))
            bottom = unsigned(r.bottom);
        unsigned left = unsigned(f.cross_x - cx), right = unsigned(f.cross_x + cx);
        if (left < unsigned(r.left))
            left = unsigned(r.left);
        if (right > unsigned(r.right))
            right = unsigned(r.right);
        const unsigned old = Call("SelectObject", f.memory_dc, f.bitmap);
        Call("BitBlt", dc, left, top, right - left, bottom - top, f.memory_dc, left - unsigned(r.left),
                        top - unsigned(r.top), 0xcc0020);
        Call("SelectObject", f.memory_dc, old);
    }
    // the cross, three pixels wide, with a hole in the middle.
    void Cross(unsigned dc, int x, int y)
    {
        const Box r = f.rainbow;
        const int cy5 = int(Call("GetSystemMetrics", 6)) * 5, cx5 = int(Call("GetSystemMetrics", 5)) * 5;
        auto maximum = [](int a, int b) { return a > b ? a : b; };
        auto minimum = [](int a, int b) { return a < b ? a : b; };
        const int top_far = maximum(y - 2 * cy5, r.top), bottom_far = minimum(y + 2 * cy5, r.bottom);
        const int left_far = maximum(x - 2 * cx5, r.left), right_far = minimum(x + 2 * cx5, r.right);
        const int top_near = maximum(y - cy5, r.top), bottom_near = minimum(y + cy5, r.bottom);
        const int left_near = maximum(x - cx5, r.left), right_near = minimum(x + cx5, r.right);
        auto line = [&](int x1, int y1, int x2, int y2) {
            Call("MoveToEx", dc, unsigned(x1), unsigned(y1), 0);
            Call("LineTo", dc, unsigned(x2), unsigned(y2));
        };
        if (top_near > r.top)
        {
            if (x - 1 >= r.left)
                line(x - 1, top_near, x - 1, top_far);
            if (r.right > x)
                line(x, top_near, x, top_far);
            if (x + 1 < r.right)
                line(x + 1, top_near, x + 1, top_far);
        }
        if (r.bottom > bottom_near)
        {
            if (x - 1 >= r.left)
                line(x - 1, bottom_near, x - 1, bottom_far);
            if (r.right > x)
                line(x, bottom_near, x, bottom_far);
            if (x + 1 < r.right)
                line(x + 1, bottom_near, x + 1, bottom_far);
        }
        if (r.left < left_near)
        {
            if (r.top <= y - 1)
                line(left_near, y - 1, left_far, y - 1);
            if (r.bottom > y)
                line(left_near, y, left_far, y);
            if (r.bottom > y + 1)
                line(left_near, y + 1, left_far, y + 1);
        }
        if (r.right > right_near)
        {
            if (r.top <= y - 1)
                line(right_near, y - 1, right_far, y - 1);
            if (r.bottom > y)
                line(right_near, y, right_far, y);
            if (r.bottom > y + 1)
                line(right_near, y + 1, right_far, y + 1);
        }
    }
    // hue, saturation or luminosity out of the cross or the arrow.
    void FromPoint(unsigned which)
    {
        switch (which)
        {
        case ColorHue: f.hue = MulDiv16(f.cross_x - f.rainbow.left, 239, f.width - 1); break;
        case ColorSat: f.sat = 240 - MulDiv16(f.cross_y - f.rainbow.top, 240, f.height - 1); break;
        case ColorLum: f.lum = 240 - MulDiv16(f.arrow_y - f.lum_bar.top, 240, f.lum_height - 1); break;
        default:
            f.hue = MulDiv16(f.cross_x - f.rainbow.left, 239, f.width);
            f.sat = 240 - MulDiv16(f.cross_y - f.rainbow.top, 240, f.height);
            f.lum = 240 - MulDiv16(f.arrow_y - f.lum_bar.top, 240, f.lum_height);
            break;
        }
    }
    // the cross or the arrow out of the colour.
    void ToPoint(unsigned which)
    {
        if (which == ColorHue || !which || (which != ColorSat && which != ColorLum))
            f.cross_x = MulDiv16(f.hue, f.width, 239) + f.rainbow.left;
        if (which == ColorSat || (which != ColorHue && which != ColorLum))
            f.cross_y = MulDiv16(240 - f.sat, f.height - 1, 240) + f.rainbow.top;
        if (which == ColorLum || (which != ColorHue && which != ColorSat))
            f.arrow_y = MulDiv16(240 - f.lum, f.lum_height - 1, 240) + f.lum_bar.top;
    }
    // The numbers in the edit fields.
    void SetInt(unsigned id, unsigned value) { Call("SetDlgItemInt", f.hwnd, id, value, 0); }
    void HlsFields(unsigned which)
    {
        if (which == ColorHue || (which != ColorSat && which != ColorLum))
            SetInt(ColorHue, unsigned(f.hue));
        if (which == ColorSat || (which != ColorHue && which != ColorLum))
            SetInt(ColorSat, unsigned(f.sat));
        if (which == ColorLum || (which != ColorHue && which != ColorSat))
            SetInt(ColorLum, unsigned(f.lum));
    }
    void RgbFields(unsigned which)
    {
        if (which == ColorRed || (which != ColorGreen && which != ColorBlue))
            SetInt(ColorRed, f.rgb & 0xff);
        if (which == ColorGreen || (which != ColorRed && which != ColorBlue))
            SetInt(ColorGreen, (f.rgb >> 8) & 0xff);
        if (which == ColorBlue || (which != ColorRed && which != ColorGreen))
            SetInt(ColorBlue, (f.rgb >> 16) & 0xff);
    }
    // hue, saturation and luminosity follow a new colour, and the
    // cross and the arrow with them.
    void FollowRgb()
    {
        int hue, sat, lum;
        RgbToHls(f.rgb, hue, sat, lum);
        if (lum != f.lum)
        {
            const unsigned dc = Dc();
            EraseArrow(dc);
            f.lum = lum;
            ToPoint(ColorLum);
            Arrow(dc, f.arrow_y);
            Release(dc);
        }
        if (hue != f.hue || sat != f.sat)
        {
            f.hue = hue;
            f.sat = sat;
            Invalidate(f.lum_bar);
            const unsigned dc = Dc();
            EraseCross(dc);
            ToPoint(ColorHue);
            ToPoint(ColorSat);
            Cross(dc, f.cross_x, f.cross_y);
            Release(dc);
        }
    }
    // the cell that holds exactly this colour, selected.
    bool Find(unsigned rgb)
    {
        unsigned i = 0;
        while (i < 0x40 && f.colors[i] != rgb)
            ++i;
        if (i >= 0x40)
            return false;
        Select(i);
        f.current = i;
        return true;
    }
    // the spectrum, painted once into a bitmap: 30 bands of
    // saturation from the top, 60 steps of hue across, at luminosity 120.
    bool Spectrum()
    {
        RgbToHls(f.rgb, f.hue, f.sat, f.lum);
        Rectangles();
        f.width = f.rainbow.right - f.rainbow.left;
        f.height = f.rainbow.bottom - f.rainbow.top;
        ToPoint(0);
        RgbFields(0);
        HlsFields(0);
        if (!f.bitmap)
        {
            const unsigned dc = Dc();
            f.bitmap = Call("CreateCompatibleBitmap", dc, unsigned(f.width), unsigned(f.height));
            Release(dc);
            if (!f.bitmap)
                return false;
        }
        const unsigned old = Call("SelectObject", f.memory_dc, f.bitmap);
        int bottom = 0;
        unsigned remaining = unsigned(f.height) * 232;
        for (unsigned sat = 240; sat; sat -= 8)
        {
            const int top = bottom;
            bottom = int(Divide(240u * unsigned(f.height) - remaining, 240));
            int right = 0;
            for (unsigned hue = 0; hue < 239; hue += 4)
            {
                const int left = right;
                right = MulDiv16(int(hue + 4), f.width, 240);
                Fill(f.memory_dc, Box{left, top, right, bottom}, HlsToRgb(hue, 120, sat));
            }
            remaining -= unsigned(f.height) * 8;
        }
        Call("SelectObject", f.memory_dc, old);
        Call("UpdateWindow", f.hwnd);
        return true;
    }
    // the spectrum out of the bitmap, and the cross over it.
    void PaintSpectrum(unsigned dc, const Box &r)
    {
        if (!f.bitmap)
            return;
        const unsigned old = Call("SelectObject", f.memory_dc, f.bitmap);
        Call("BitBlt", dc, unsigned(r.left), unsigned(r.top), unsigned(r.right - r.left),
                        unsigned(r.bottom - r.top), f.memory_dc, unsigned(r.left - f.rainbow.left),
                        unsigned(r.top - f.rainbow.top), 0xcc0020);
        Call("SelectObject", f.memory_dc, old);
        Cross(dc, f.cross_x, f.cross_y);
        Call("UpdateWindow", f.hwnd);
    }
    // what of the right half lies in `area`.
    void PaintParts(unsigned dc, const Box &area)
    {
        Box r;
        if (Intersect(area, f.left_half, r))
            Fill(dc, r, f.rgb);
        if (Intersect(area, f.solid_half, r))
            Fill(dc, r, Call("GetNearestColor", dc, f.rgb));
        if (Intersect(area, f.lum_bar, r))
        {
            Box band{f.lum_bar.left, f.lum_bar.bottom - 4, f.lum_bar.right, f.lum_bar.bottom};
            Fill(dc, band, HlsToRgb(unsigned(f.hue), 0, unsigned(f.sat)));
            for (unsigned lum = 8; lum < 240; lum += 8)
            {
                band.bottom = band.top;
                band.top = int(Divide(unsigned(f.lum_bar.bottom + 4) * 240 - unsigned(f.lum_height) * (lum + 8), 240));
                Fill(dc, band, HlsToRgb(unsigned(f.hue), lum, unsigned(f.sat)));
            }
            band.bottom = band.top;
            band.top = f.lum_bar.top;
            Fill(dc, band, HlsToRgb(unsigned(f.hue), 240, unsigned(f.sat)));
        }
        if (Intersect(area, f.arrow, r))
            Arrow(dc, f.arrow_y);
        if (Intersect(area, f.rainbow, r))
            PaintSpectrum(dc, r);
    }
    // WM_PAINT.
    void Paint(unsigned dc, const Box &area)
    {
        for (unsigned i = 0; i < BasicColors; ++i)
            Cell(dc, i);
        for (unsigned i = 0x30; i < 0x40; ++i)
            Cell(dc, i);
        const unsigned focus = User::GetFocus();
        if (focus == Item(ColorBox))
            Focus(f.basic);
        else if (focus == Item(ColorCustom))
            Focus(f.custom);
        PaintParts(dc, area);
    }
    // WM_INITDIALOG.
    unsigned Initialize(unsigned wp, bool recorded)
    {
        const unsigned solid = Item(ColorSolid);
        if (!f.memory_dc)
        {
            const unsigned dc = Dc();
            f.memory_dc = Call("CreateCompatibleDC", dc);
            Release(dc);
            if (!f.memory_dc)
                return 0;
        }
        f.ready = true;
        Rectangles();
        const unsigned flags = Flags();
        f.rgb = flags & CcRgbInit ? Nearest(app.Read32(f.cc + CcResult)) : 0;
        if (flags & (CcFullOpen | CcPreventFullOpen))
            User::EnableWindow(Item(ColorMix), false);
        if (flags & CcSolidColor)
            User::ShowWindow(Item(ColorSolidRight), 0);
        if (flags & CcFullOpen)
        {
            Spectrum();
            f.full = true;
            RgbToHls(f.rgb, f.hue, f.sat, f.lum);
            RgbFields(0);
            HlsFields(0);
        }
        else
        {
            for (unsigned id = ColorHue; id <= ColorBlue; ++id)
                User::EnableWindow(Item(id), false);
            for (unsigned id = ColorHueAccel; id <= ColorBlueAccel; ++id)
                User::EnableWindow(Item(id), false);
            User::EnableWindow(Item(ColorAdd), false);
            User::EnableWindow(solid, false);
            f.full = false;
            const unsigned at = f.scratch;
            Call("GetWindowRect", Item(ColorBox), at);
            const Box box = ReadRect(at);
            Call("GetWindowRect", Item(ColorRainbow), at);
            const Box rainbow = ReadRect(at);
            Call("GetWindowRect", f.hwnd, at);
            f.dialog = ReadRect(at);
            Call("MoveWindow", f.hwnd, unsigned(f.dialog.left), unsigned(f.dialog.top),
                                unsigned((rainbow.left + box.right) / 2 - f.dialog.left),
                                unsigned(f.dialog.bottom - f.dialog.top), 0);
        }
        Cells();
        const unsigned custom = Win16FarPointer(f.cc + CcCustom);
        for (unsigned i = 0; i < 16; ++i)
            f.colors[0x30 + i] = custom ? app.Read32(custom + i * 4) : 0;
        f.basic = 0;
        f.current = 0;
        f.custom = 0x30;
        Find(f.rgb);
        if (f.custom > f.current)
            f.basic = f.current;
        else
            f.custom = f.current;
        if (!(flags & CcShowHelp))
        {
            const unsigned help = Item(PushHelp);
            User::EnableWindow(help, false);
            User::ShowWindow(help, 0);
        }
        const unsigned style = Call("GetWindowLongA", solid, unsigned(-16));
        Call("SetWindowLongA", solid, unsigned(-16), style & ~0x10000u);
        f.initializing = false;
        if (flags & CcEnableHook)
        {
            if (recorded) app.GiveTail(TailRecord);
            return Hook(0x110, wp, f.cc_far);
        }
        return 1;
    }
    unsigned Win16FarPointer(unsigned at) { return app.Win16FarPointer(at); }
    // OK, Cancel or 3 end the dialog.
    unsigned Close(unsigned id, unsigned lp)
    {
        if (Win16ColorFrame::capture)
        {
            Win16ColorFrame::capture = false;
            Call("ReleaseCapture");
            Call("ClipCursor", 0);
        }
        unsigned result = 0;
        if (id == 1 || id == 2)
        {
            if (id == 1)
                app.Write32(f.cc + CcResult, f.rgb);
            const unsigned custom = Win16FarPointer(f.cc + CcCustom);
            for (unsigned i = 0; custom && i < 16; ++i)
                app.Write32(custom + i * 4, f.colors[0x30 + i]);
            result = id == 1;
            // commdlg_ColorOK goes to the hook whenever the structure names
            // one, and a nonzero answer keeps the dialog open.
            if (result && app.Read32(f.cc + CcHook))
            {
                const unsigned arguments[] = {f.hwnd, f.message_ok, 0, f.cc_far};
                const unsigned hook = app.Win16Mark(app.Read32(f.cc + CcHook));
                Win16ControllerPhase phase(app, HostWin16Color, f.hwnd, Win16ControllerPhase::ColorOK,
                                           0, result, 0, recorded);
                if (phase.Callback(hook, arguments, 4)) return 1;
                FinishClose(result);
                return 1;
            }
        }
        else
            result = lp & 0xffff;
        FinishClose(result);
        return 1;
    }
    void FinishClose(unsigned result)
    {
        if (f.memory_dc)
            Call("DeleteDC", f.memory_dc);
        f.memory_dc = 0;
        if (f.bitmap)
            Call("DeleteObject", f.bitmap);
        f.bitmap = 0;
        app.EndDialog(f.hwnd, result);
    }

    // EN_CHANGE of hue, saturation, luminosity.
    void HlsChanged(unsigned id)
    {
        const unsigned flag = f.scratch + 0x80;
        unsigned value = Call("GetDlgItemInt", f.hwnd, id, flag, 0);
        const unsigned ok = app.Read32(flag) & 0xffff;
        if (!ok)
        {
            const unsigned text = f.scratch + 0x100;
            if (Call("GetDlgItemTextA", f.hwnd, id, text, 2))
            {
                HlsFields(id);
                Call("SendDlgItemMessageA", f.hwnd, id, 0xb1, 0, 0x7fff);
            }
            return;
        }
        const unsigned limit = id == ColorHue ? 239 : 240;
        if (value > limit)
        {
            SetInt(id, limit);
            value = limit;
        }
        int &slot = id == ColorHue ? f.hue : id == ColorSat ? f.sat : f.lum;
        if (slot == int(value))
            return;
        const unsigned dc = Dc();
        if (id == ColorLum)
        {
            EraseArrow(dc);
            f.lum = int(value);
            ToPoint(ColorLum);
            f.rgb = Nearest(HlsToRgb(unsigned(f.hue), unsigned(f.lum), unsigned(f.sat)));
            RgbFields(0);
            Arrow(dc, f.arrow_y);
            Release(dc);
            Invalidate(f.sample);
            Call("UpdateWindow", f.hwnd);
            return;
        }
        EraseCross(dc);
        slot = int(value);
        f.rgb = Nearest(HlsToRgb(unsigned(f.hue), unsigned(f.lum), unsigned(f.sat)));
        RgbFields(0);
        ToPoint(id);
        Cross(dc, f.cross_x, f.cross_y);
        Release(dc);
        Invalidate(f.lum_bar);
        Invalidate(f.sample);
        Call("UpdateWindow", f.hwnd);
    }
    // Red, green and blue.
    void RgbChanged(unsigned id, unsigned code)
    {
        const unsigned flag = f.scratch + 0x80;
        unsigned value = Call("GetDlgItemInt", f.hwnd, id, flag, 0);
        const unsigned ok = app.Read32(flag) & 0xffff;
        if (code == 0x200) // EN_KILLFOCUS: an unreadable field gets its value back
        {
            if (!ok)
                RgbFields(id);
            return;
        }
        if (code != 0x300)
            return;
        if (ok)
        {
            if (value > 255)
            {
                value = 255;
                SetInt(id, 255);
            }
            const unsigned shift = (id - ColorRed) * 8;
            if (((f.rgb >> shift) & 0xff) != value)
            {
                f.rgb = (f.rgb & ~(0xffu << shift)) | value << shift;
                FollowRgb();
                HlsFields(id);
            }
        }
        else
        {
            const unsigned text = f.scratch + 0x100;
            if (Call("GetDlgItemTextA", f.hwnd, id, text, 2))
            {
                RgbFields(id);
                Call("SendDlgItemMessageA", f.hwnd, id, 0xb1, 0, 0x7fff);
            }
        }
        Invalidate(f.sample);
    }
    // "Define Custom Colors >>".
    void Open()
    {
        Hourglass(true);
        Spectrum();
        for (unsigned id = ColorHue; id <= ColorBlue; ++id)
            User::EnableWindow(Item(id), true);
        for (unsigned id = ColorHueAccel; id <= ColorBlueAccel; ++id)
            User::EnableWindow(Item(id), true);
        User::EnableWindow(Item(ColorAdd), true);
        User::EnableWindow(Item(ColorSolid), true);
        User::EnableWindow(Item(ColorMix), false);
        app.Send(f.hwnd, 0x28, Item(ColorHue), 1);
        const unsigned at = f.scratch;
        Call("GetWindowRect", f.hwnd, at);
        const Box now = ReadRect(at);
        Call("SetWindowPos", f.hwnd, 0, unsigned(f.dialog.left), unsigned(f.dialog.top),
                              unsigned(f.dialog.right - f.dialog.left), unsigned(f.dialog.bottom - f.dialog.top),
                              0x16);
        Invalidate(Box{f.dialog.left, now.top, now.left, now.bottom});
        Hourglass(false);
        f.full = true;
    }
    // the solid colour closest to the current one.
    void Solid()
    {
        const unsigned dc = Dc();
        EraseCross(dc);
        EraseArrow(dc);
        f.rgb = Call("GetNearestColor", dc, f.rgb);
        RgbToHls(f.rgb, f.hue, f.sat, f.lum);
        ToPoint(0);
        Cross(dc, f.cross_x, f.cross_y);
        Arrow(dc, f.arrow_y);
        Release(dc);
        HlsFields(0);
        RgbFields(0);
        Invalidate(f.sample);
        Invalidate(f.lum_bar);
    }
    void Capture(const Box &b)
    {
        if (Win16ColorFrame::capture)
            return;
        Call("SetCapture", f.hwnd);
        const unsigned at = Rect(b);
        Call("ClientToScreen", f.hwnd, at);
        Call("ClientToScreen", f.hwnd, at + 8);
        Call("ClipCursor", at);
        Win16ColorFrame::capture = true;
    }
    // a press or, with the capture, a move.
    unsigned Mouse(unsigned message, int x, int y)
    {
        if (Inside(f.rainbow, x, y))
        {
            if (message == 0x201)
            {
                const unsigned dc = Dc();
                EraseCross(dc);
                Release(dc);
            }
            f.cross_x = x;
            FromPoint(ColorHue);
            HlsFields(ColorHue);
            f.cross_y = y;
            FromPoint(ColorSat);
            HlsFields(ColorSat);
            f.rgb = Nearest(HlsToRgb(unsigned(f.hue), unsigned(f.lum), unsigned(f.sat)));
            const unsigned dc = Dc();
            PaintParts(dc, f.lum_bar);
            PaintParts(dc, f.sample);
            Release(dc);
            RgbFields(0);
            Capture(f.rainbow);
            return 1;
        }
        if (Inside(f.lum_bar, x, y) || Inside(f.arrow, x, y))
        {
            const unsigned dc = Dc();
            EraseArrow(dc);
            f.arrow_y = y;
            Arrow(dc, y);
            FromPoint(ColorLum);
            HlsFields(ColorLum);
            f.rgb = Nearest(HlsToRgb(unsigned(f.hue), unsigned(f.lum), unsigned(f.sat)));
            PaintParts(dc, f.sample);
            Release(dc);
            Validate(f.arrow);
            Validate(f.sample);
            RgbFields(0);
            Capture(f.arrow);
            return 1;
        }
        if (message != 0x201)
            return 1;
        const unsigned child = Call("ChildWindowFromPoint", f.hwnd, unsigned(x), unsigned(y));
        Box area;
        unsigned rows, base;
        if (child == Item(ColorBox))
        {
            area = Box{f.cells[0].left, f.cells[0].top, f.cells[47].right + 5, f.cells[47].bottom + 5};
            rows = 6;
            base = 0;
        }
        else if (child == Item(ColorCustom))
        {
            area = Box{f.cells[48].left, f.cells[48].top, f.cells[63].right + 5, f.cells[63].bottom + 5};
            rows = 2;
            base = 0x30;
        }
        else
            return 0;
        if (User::GetFocus() != child)
            app.Send(f.hwnd, 0x28, child, 1);
        if (!Inside(area, x, y))
            return 1;
        const int dx = x - area.left, dy = y - area.top;
        if (dx % f.cell_width >= f.cell_width - 5 || dy % f.cell_height >= f.cell_height - 5)
            return 1;
        const unsigned index =
            unsigned((dy * int(rows) / (area.bottom - area.top)) * 8 + dx * 8 / (area.right - area.left)) + base;
        if (!(index < BasicColors || index >= 0x30))
            return 1;
        Select(index);
        f.current = index;
        Focus(index);
        if (index >= 0x30)
            f.custom = index;
        else
            f.basic = index;
        f.rgb = Nearest(f.colors[index]);
        const unsigned dc = Dc();
        if (f.full)
        {
            FollowRgb();
            HlsFields(0);
            RgbFields(0);
            PaintParts(dc, f.sample);
        }
        Cell(dc, f.basic);
        Cell(dc, f.custom);
        Release(dc);
        return 1;
    }
    // the button goes up.
    void Up(int x, int y)
    {
        if (!Win16ColorFrame::capture)
            return;
        Win16ColorFrame::capture = false;
        Call("ReleaseCapture");
        Call("ClipCursor", 0);
        if (Inside(f.rainbow, x, y))
        {
            const unsigned dc = Dc();
            f.cross_x = x;
            f.cross_y = y;
            Cross(dc, x, y);
            PaintParts(dc, f.lum_bar);
            Release(dc);
            Validate(f.rainbow);
        }
        else if (Inside(f.lum_bar, x, y))
        {
            const unsigned dc = Dc();
            Arrow(dc, f.arrow_y);
            Release(dc);
            Validate(f.lum_bar);
        }
    }
};

// ChooseColor (COMMDLG.5).
bool Application::Win16ChooseColor(const Win16Frame &frame, unsigned &result)
{
    const bool gate = TakeFresh();
    result = 0;
    const unsigned cc = frame.Pointer(0);
    Win16CommonDialogError(0);
    if (!cc || Read32(cc) != ChooseColour16)
    {
        Win16CommonDialogError(ErrorStructSize);
        return true;
    }
    unsigned flags = Read32(cc + CcFlags);
    unsigned hook = 0;
    unsigned instance = 0;
    if (flags & CcEnableHook)
    {
        if (!Read32(cc + CcHook))
        {
            Win16CommonDialogError(0xb); // CDERR_NOHOOK
            return true;
        }
        hook = Win16Mark(Read32(cc + CcHook));
        const unsigned owner = Win16Word(cc + CcOwner);
        if (owner)
        {
            unsigned word = 0;
            User::WindowLong(owner, -6, word, false);
            instance = word & 0xffff;
        }
        else if ((flags & CcEnableTemplate) && Win16Word(cc + CcInstance))
            instance = Win16Word(cc + CcInstance);
    }
    else
        Write32(cc + CcHook, 0);
    // The template: the caller's by handle or by name, or COMMDLG's own
    // "ChooseColor" (string 303h).
    unsigned char *converted = nullptr;
    unsigned length = 0, native = 0;
    if (flags & CcEnableTemplateHandle)
    {
        if (!Win16Word(cc + CcInstance))
        {
            Win16CommonDialogError(3); // CDERR_NOTEMPLATE
            return true;
        }
        native = Win16NativeTemplate(0, 0, Win16Word(cc + CcInstance));
    }
    else if (flags & CcEnableTemplate)
    {
        if (!Read32(cc + CcTemplate))
        {
            Win16CommonDialogError(3);
            return true;
        }
        if (!Win16Word(cc + CcInstance))
        {
            Win16CommonDialogError(4); // CDERR_NOHINSTANCE
            return true;
        }
        native = Win16NativeTemplate(Win16Word(cc + CcInstance), Read32(cc + CcTemplate), 0);
        if (!native)
        {
            Win16CommonDialogError(6); // CDERR_FINDRESFAILURE
            return true;
        }
    }
    else
    {
        auto &images = CommDlgResources();
        unsigned size = 0;
        char name[32] = {};
        const unsigned char *bytes = images.file && NeString(images.file, images.length, 0x303, name, sizeof name)
                                         ? NeNamedResource(images.file, images.length, 5, name, size)
                                         : nullptr;
        converted = bytes ? new unsigned char[4 * size + 64] : nullptr;
        if (!converted || !ConvertDialogTemplate(bytes, size, converted, 4 * size + 64, length))
        {
            delete[] converted;
            Win16CommonDialogError(6);
            return true;
        }
    }
    // a program for Windows 3.0 or older gets no WM_HELP help.
    const unsigned module_instance = instance ? instance : (win16.state->module_count ? win16.state->modules[0].instance : 0);
    if (Win16ModuleOf(module_instance)->image.ExpectedVersion() <= 0x30a)
    {
        flags |= CcOldApplication;
        Write32(cc + CcFlags, flags);
    }
    auto *held = new Win16ColorFrame(this);
    struct Holder { Win16ColorFrame *frame; ~Holder() { delete frame; } } release{held};
    auto &f = *held;
    f.cc = cc;
    f.cc_far = frame.Word(1) << 16 | frame.Word(0);
    f.hook = hook;
    f.scratch = Allocate(1024);
    const unsigned ok = String("commdlg_ColorOK"), rgb = String("commdlg_SetRGBColor"), help = String("commdlg_help");
    f.message_ok = Api("RegisterWindowMessageA", &ok);
    f.message_rgb = Api("RegisterWindowMessageA", &rgb);
    f.message_help = Api("RegisterWindowMessageA", &help);
    Free(ok);
    Free(rgb);
    Free(help);
    const unsigned owner = Win16Word(cc + CcOwner);
    const unsigned module = win16.state && win16.state->module_count ? win16.state->modules[0].handle : 0;
    unsigned hwnd = 0;
    const bool kept = gate && PushHostFrame(HostWin16Color, 0, 1);
    f.native_template = native;
    if (native)
    {
        const unsigned locked = Api("GlobalLock", &native);
        const unsigned size = Api("GlobalSize", &native);
        auto *copy = locked && size ? new unsigned char[size] : nullptr;
        if (copy && Read(locked, copy, size))
        {
            if (kept) GiveTail(TailRecord);
            hwnd = CreateDialogTemplate(copy, size, owner, Win16ColorDialogToken, cc, true, instance ? instance : module);
        }
        delete[] copy;
        Api("GlobalUnlock", &native);
        Api("GlobalFree", &native);
        f.native_template = 0;
    }
    else
    {
        // COMMDLG's own template goes out with COMMDLG's instance, so the
        // dialog is a window of a 4.0 module.
        if (kept) GiveTail(TailRecord);
        hwnd = CreateDialogTemplate(converted, length, owner, Win16ColorDialogToken, cc, true, Win16SystemModule("COMMDLG"));
        delete[] converted;
    }
    f.hwnd = hwnd;
    if (kept)
    {
        auto &record = threads[current_thread].host_frames[threads[current_thread].host_frame_count - 1];
        record.a = hwnd; record.b = 0;
    }
    unsigned answer = 0;
    if (hwnd && state != Failed && f.ready)
        answer = RunModal(hwnd, kept);
    else if (state != Failed)
        Win16CommonDialogError(0xffff); // CDERR_DIALOGFAILURE
    if (kept) PopHostFrame();
    release.frame = nullptr;
    result = FinishWin16ColorDialog(hwnd, answer);
    return true;
}
unsigned Application::FinishWin16ColorDialog(unsigned hwnd, unsigned result)
{
    auto *frame = Win16ColorFrameOf(this, hwnd, false);
    if (!frame) return 0;
    if (frame->native_template)
    {
        Api("GlobalUnlock", &frame->native_template);
        Api("GlobalFree", &frame->native_template);
        frame->native_template = 0;
    }
    if (frame->memory_dc) Api("DeleteDC", &frame->memory_dc);
    if (frame->bitmap) Api("DeleteObject", &frame->bitmap);
    if (frame->scratch) Free(frame->scratch);
    delete frame;
    return result;
}
void Application::DiscardSavedWin16ColorDialogs()
{
    for (auto *f = Win16ColorFrame::head, *next = f; f; f = next)
    {
        next = f->next;
        if (f->app != this) continue;
        FinishWin16ColorDialog(f->hwnd, 0);
    }
}

// The dialog procedure.
unsigned Application::Win16ColorDialogMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned tail = TakeTail();
    auto *frame = Win16ColorFrameOf(this, hwnd, message == 0x110);
    if (!frame)
        return 0;
    Win16ColorDialog e{*this, *frame};
    auto &f = *frame;
    if (message == 0x110)
    {
        // the wait cursor around the setup, the dialog shown after it.
        e.Hourglass(true);
        bool resumed = false, pushed = false;
        auto &thread = threads[current_thread];
        if (thread.host_resume && thread.host_resume_at < thread.host_frame_count)
        {
            const auto &record = thread.host_frames[thread.host_resume_at];
            if (record.kind == HostWin16Color && record.a == hwnd && record.b == (0x80000000u | message))
            {
                resumed = pushed = true;
                ++thread.host_resume_at;
            }
        }
        if (!resumed && tail == TailRecord)
            pushed = PushHostFrame(HostWin16Color, hwnd, 0x80000000u | message, wp, lp);
        struct Record { Application &app; bool pushed; ~Record() { if(pushed) app.PopHostFrame(); } } record{*this, pushed};
        if (!resumed) f.initializing = true;
        const unsigned answer = resumed ? ResumeInner() & 0xffff : e.Initialize(wp, pushed);
        e.Hourglass(false);
        if (!f.ready)
        {
            EndDialog(hwnd, 0);
            return 0;
        }
        if (answer)
            User::ShowWindow(hwnd, 1);
        return answer;
    }
    Win16ControllerPhase phase(*this, HostWin16Color, hwnd, Win16ControllerPhase::Message,
                               message, wp, lp, tail == TailRecord && !f.initializing && f.ready);
    if (phase.AfterHook()) return phase.Rest();
    if (!phase.resumed && !f.ready) return 0;
    if (phase.resumed || (f.hook && (e.Flags() & CcEnableHook)))
    {
        const unsigned arguments[] = {hwnd, message, wp, lp};
        if (const unsigned answer = phase.Callback(f.hook, arguments, 4)) return answer;
    }
    phase.Continue();
    e.recorded = phase.kept;
    const int x = int(short(lp & 0xffff)), y = int(short(lp >> 16));
    switch (message)
    {
    case 0x3: // WM_MOVE
        e.Rectangles();
        return 0;
    case 0xf: // WM_PAINT
    {
        const unsigned paint = f.scratch + 0x200;
        const unsigned a[] = {hwnd, paint};
        const unsigned dc = Api("BeginPaint", a);
        Box area = e.ReadRect(paint + 8);
        e.Paint(dc, area);
        Api("EndPaint", a);
        return 1;
    }
    case 0x53: // WM_HELP
        if (!(e.Flags() & CcOldApplication))
        {
            const unsigned table = f.scratch + 0x300;
            Store(table, HelpTable, sizeof HelpTable);
            e.Call("WinHelpA", Read32(lp + 12), 0, 0xc, table);
        }
        return 1;
    case 0x7b: // WM_CONTEXTMENU
    {
        const unsigned table = f.scratch + 0x300;
        Store(table, HelpTable, sizeof HelpTable);
        e.Call("WinHelpA", wp, 0, 0xa, table);
        return 1;
    }
    case 0x87: // WM_GETDLGCODE
        return 0xd;
    case 0x100: // WM_KEYDOWN
    {
        unsigned index = 0;
        if (e.Key(wp & 0xffff, index))
            e.Focus(index);
        return 1;
    }
    case 0x102: // WM_CHAR: the space bar takes the cell with the focus
    {
        if ((wp & 0xffff) != ' ')
            return 1;
        const unsigned focus = User::GetFocus();
        unsigned index;
        if (focus == e.Item(ColorBox))
            index = f.basic;
        else if (focus == e.Item(ColorCustom))
            index = f.custom;
        else
            return 0;
        f.rgb = e.Nearest(f.colors[index & 63]);
        e.FollowRgb();
        e.Invalidate(f.sample);
        e.Select(index);
        f.current = index;
        if (f.full)
        {
            e.HlsFields(0);
            e.RgbFields(0);
        }
        return 1;
    }
    case 0x111: // WM_COMMAND
    {
        const unsigned id = wp & 0xffff, code = wp >> 16;
        switch (id)
        {
        case 1:
        case 2:
        case 3:
            return e.Close(id, lp);
        case ColorHue:
        case ColorSat:
        case ColorLum:
            if (code == 0x300)
                e.HlsChanged(id);
            return 1;
        case ColorRed:
        case ColorGreen:
        case ColorBlue:
            e.RgbChanged(id, code);
            return 1;
        case ColorAdd:
        {
            f.colors[f.custom & 63] = f.rgb;
            e.Invalidate(f.cells[f.custom & 63]);
            if (f.custom >= 0x3f)
                f.custom = 0x30;
            else if (f.custom >= 0x38)
                f.custom -= 7;
            else
                f.custom += 8;
            return 1;
        }
        case ColorSolid:
            e.Solid();
            return 1;
        case ColorMix:
            e.Open();
            return 1;
        case PushHelp:
            if (f.message_help && Win16Word(f.cc + CcOwner))
            {
                Win16ControllerPhase help(*this, HostWin16Color, hwnd, Win16ControllerPhase::OwnerHelp,
                                           0, 1, 0, phase.kept);
                help.Help(Win16Word(f.cc + CcOwner), f.message_help, hwnd, f.cc_far);
            }
            return 1;
        default:
            return 1;
        }
    }
    case 0x200: // WM_MOUSEMOVE
        if (!Win16ColorFrame::capture)
            return 1;
        return e.Mouse(message, x, y);
    case 0x201: // WM_LBUTTONDOWN
        return e.Mouse(message, x, y);
    case 0x202: // WM_LBUTTONUP
        e.Up(x, y);
        return 1;
    case 0x203: // WM_LBUTTONDBLCLK
        if (Win16ColorDialog::Inside(f.solid_half, x, y))
            e.Solid();
        return 1;
    case 0x30f: // WM_QUERYNEWPALETTE: the dialog holds no palette
    case 0x311: // WM_PALETTECHANGED
        return 1;
    default:
        break;
    }
    if (message == f.message_rgb)
    {
        // commdlg_SetRGBColor with the colour in lParam.
        if (e.Find(lp))
        {
            f.rgb = e.Nearest(lp);
            if (f.custom > f.current)
                f.basic = f.current;
            else
                f.custom = f.current;
        }
        if (f.full)
        {
            f.rgb = e.Nearest(lp);
            e.FollowRgb();
            e.HlsFields(0);
            e.RgbFields(0);
            const unsigned dc = e.Dc();
            e.PaintParts(dc, f.sample);
            e.Release(dc);
        }
        return 1;
    }
    return 0;
}

// the subclass of the two cell boxes. The focus draws the focus
// rectangle of the box's current cell, the keys go to the dialog.
unsigned Application::Win16ColorBoxMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    const unsigned parent = User::Parent(hwnd);
    auto *frame = Win16ColorFrameOf(this, parent, false);
    switch (message)
    {
    case 0x7: // WM_SETFOCUS
    case 0x8: // WM_KILLFOCUS
    {
        if (!frame)
            break;
        Win16ColorDialog e{*this, *frame};
        unsigned id = 0;
        User::WindowLong(hwnd, -12, id, false);
        const unsigned index = id == ColorBox ? frame->basic : frame->custom;
        const unsigned dc = e.Dc();
        e.Call("DrawFocusRect", dc, e.Rect(Win16ColorDialog::Grow(frame->cells[index & 63], 3)));
        e.Release(dc);
        return 1;
    }
    case 0x87: // WM_GETDLGCODE: DLGC_WANTCHARS | DLGC_WANTARROWS
        return 0x81;
    case 0x100: // WM_KEYDOWN
    case 0x102: // WM_CHAR
        return Send(parent, message, wp, lp);
    default:
        break;
    }
    const unsigned previous = Win16ColorFrame::box_procedure;
    if (previous && previous != Win16ColorBoxToken)
    {
        const unsigned a[] = {previous, hwnd, message, wp, lp};
        return Api("CallWindowProcA", a);
    }
    return StaticProcedure(hwnd, message, wp, lp);
}
