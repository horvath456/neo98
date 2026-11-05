// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 DrawStateA, GrayStringA and the tabbed text pair. All four are word
// thunks of USER.EXE, and all four do their work with nothing but public GDI
// calls, so they stay out of the GDI files: DrawState paints into a
// monochrome bitmap and folds it onto the target with two raster operations,
// and the tabbed text routines need the average character width that
// GetTextMetrics already reports.
#include "x86_app.h"
#include <string.h>
namespace X86 {
namespace {
// DrawState flags. USER reads the low three bits as the type and
// turns the pattern bits into the internal "go through the monochrome
// bitmap" bit 80h.
constexpr unsigned TypeMask = 7;
constexpr unsigned TypeComplex = 0, TypeText = 1, TypePrefixText = 2;
constexpr unsigned TypeIcon = 3, TypeBitmap = 4;
constexpr unsigned CallbackBit = 8;      // a callback replaces the type
constexpr unsigned StateUnion = 0x10, StateDisabled = 0x20;
constexpr unsigned StateMono = 0x80, StateRight = 0x8000;
constexpr unsigned PatternBits = 0x170;  //
// The two raster operations of the technique. ORs the gray pattern
// into the monochrome image, so every second pixel turns into background, and
// folds that image onto the target through the foreground brush.
constexpr unsigned RopWhiteness = 0x00ff0062, RopPatternOr = 0x00fa0089;
constexpr unsigned RopMaskThroughBrush = 0x00b8074a;
// COLOR_3DSHADOW and COLOR_3DHILIGHT, the two brushes and Windows 98 take
// for the embossed look of a disabled item.
constexpr unsigned ColorShadow = 16, ColorHilight = 20;
} // namespace

// The monochrome image becomes foreground where it is black: the
// target keeps black text and white background around the blit, the brush
// supplies the colour, and 00B8074Ah is PSDPxax.
bool Application::DrawStateBlit(unsigned hdc, unsigned brush, unsigned mono,
                                int x, int y, int cx, int cy)
{
    const unsigned old_background = Gdi::SetBkColor(hdc, 0);
    const unsigned old_text = Gdi::SetTextColor(hdc, 0xffffff);
    const unsigned old_brush = brush ? Gdi::SelectObject(hdc, brush) : 0;
    const bool drawn = Gdi::BitBlt(hdc, x, y, cx, cy, mono, 0, 0, RopMaskThroughBrush);
    if (brush) Gdi::SelectObject(hdc, old_brush);
    Gdi::SetTextColor(hdc, old_text);
    Gdi::SetBkColor(hdc, old_background);
    return drawn;
}

// Windows 98 keeps one 50 percent pattern brush in USER data 0AC8h. NEO98 builds
// the same eight by eight checkerboard for the one blit that needs it and
// gives it back afterwards, so nothing of it outlives the call.
unsigned Application::HalftoneBrush(unsigned &pattern)
{
    const unsigned char bits[16] = {0xaa, 0, 0x55, 0, 0xaa, 0, 0x55, 0,
                                    0xaa, 0, 0x55, 0, 0xaa, 0, 0x55, 0};
    const unsigned memory = Allocate(sizeof bits);
    if (!memory) return 0;
    Store(memory, bits, sizeof bits);
    const unsigned arguments[] = {8, 8, 1, 1, memory};
    pattern = GdiApi("CreateBitmap", arguments);
    Free(memory);
    if (!pattern) return 0;
    const unsigned brush = Gdi::CreatePatternBrush(pattern);
    if (!brush) { Api("DeleteObject", &pattern); pattern = 0; }
    return brush;
}

unsigned Application::DrawStateApi(ApiName name, const unsigned *a)
{
    // USER is the whole of GrayString: it pushes 19h as the tenth
    // argument and jumps into DrawState. 19h is DST_TEXT, the callback bit and
    // DSS_UNION, so a null callback draws the text itself, a callback takes
    // over, and either way the result goes through the gray pattern.
    unsigned widened[10];
    if (API_IS(name, "GrayStringA"))
    {
        for (unsigned i = 0; i < 9; ++i) widened[i] = a[i];
        widened[9] = 0x19;
        a = widened;
    }
    const unsigned hdc = a[0], brush = a[1], callback = a[2], data = a[3];
    unsigned parameter = a[4], flags = a[9];
    int x = int(a[5]), y = int(a[6]), cx = int(a[7]), cy = int(a[8]);
    if (!Gdi::ValidDC(hdc)) { last_error = 1400; return 0; }
    // Any of the pattern bits routes the drawing through the
    // monochrome bitmap, because only there do the two raster operations work.
    if (flags & PatternBits) flags |= StateMono;
    unsigned type = flags & TypeMask;
    if (type > 5) return 0;                       //
    Gdi::SIZE extent = {0, 0};
    const char *text = nullptr;
    if (type == TypeText || type == TypePrefixText)
    {
        // a length of zero means the caller passed a C string.
        text = ReadString(data);
        if (!text) { last_error = 87; return 0; }
        if (!parameter) parameter = unsigned(strlen(text));
        // a missing extent comes from the text itself.
        if (!cx || !cy)
        {
            if (!Gdi::GetTextExtentPoint32A(hdc, text, int(parameter), &extent)) return 0;
            if (!cx) cx = extent.cx;
            if (!cy) cy = extent.cy;
        }
        // with the callback bit set and a callback present the type
        // becomes DST_COMPLEX and the caller draws.
        if ((flags & CallbackBit) && callback) type = TypeComplex;
    }
    else if (type == TypeIcon || type == TypeBitmap)
    {
        // and Windows 98 take the object out of lData and ask it for the
        // extent the caller left at zero.
        parameter = data;
        if (!parameter) return 0;
        if (!cx || !cy)
        {
            unsigned width = 0, height = 0;
            if (type == TypeIcon)
            {
                auto *icon = FindIcon(parameter);
                if (!icon || !icon->data) { last_error = 6; return 0; }
                width = icon->data->width;
                height = icon->data->height;
            }
            else
            {
                unsigned bitmap[6] = {};
                if (!Gdi::BitmapInfo(parameter, bitmap)) { last_error = 6; return 0; }
                width = bitmap[1];
                height = bitmap[2];
            }
            if (!cx) cx = int(width);
            if (!cy) cy = int(height);
        }
    }
    else if (type == TypeComplex && !callback)
        return 0;                                 //
    else if (type == 5)
        return 0;                                 // no public form reaches it
    // without an extent there is nothing to draw unless the callback
    // measures for itself.
    if ((!cx || !cy) && !(flags & CallbackBit)) return 1;
    auto draw = [&](unsigned target, int at_x, int at_y) -> unsigned {
        switch (type)
        {
        case TypeComplex:
        {
            const unsigned arguments[] = {target, data, parameter,
                                          unsigned(cx), unsigned(cy)};
            return Call(callback, arguments, 5);
        }
        case TypeText:
        case TypePrefixText:
        {
            int offset = 0;
            // DSS_RIGHT puts the text at the right edge of the box.
            if (flags & StateRight)
            {
                if (!extent.cx)
                    Gdi::GetTextExtentPoint32A(target, text, int(parameter), &extent);
                offset = cx - extent.cx;
            }
            if (type == TypePrefixText)
            {
                // The prefix form uses DrawText.
                Gdi::RECT box = {at_x + offset, at_y, at_x + cx, at_y + cy};
                unsigned rect = Allocate(16);
                if (!rect) return 0;
                const int values[4] = {box.left, box.top, box.right, box.bottom};
                Store(rect, values, 16);
                const unsigned arguments[] = {target, data, parameter, rect, 0};
                const unsigned drawn = Api("DrawTextA", arguments);
                Free(rect);
                return drawn;
            }
            return Gdi::TextOutA(target, at_x + offset, at_y, text, int(parameter)) ? 1u : 0u;
        }
        case TypeIcon:
        {
            const unsigned arguments[] = {target, unsigned(at_x), unsigned(at_y), parameter};
            return Api("DrawIcon", arguments);
        }
        default:
        {
            // Windows 98 selects the bitmap into a memory DC and copies it over.
            const unsigned memory = Gdi::CreateCompatibleDC(target);
            if (!memory) return 0;
            const unsigned keep = Gdi::SelectObject(memory, parameter);
            const bool copied = Gdi::BitBlt(target, at_x, at_y, cx, cy, memory, 0, 0, 0x00cc0020);
            Gdi::SelectObject(memory, keep);
            Gdi::DeleteDC(memory);
            return copied ? 1u : 0u;
        }
        }
    };
    // without a pattern the item goes straight onto the target.
    if (!(flags & StateMono)) return draw(hdc, x, y) ? 1u : 0u;
    // Windows 98 builds a bitmap one pixel wider than the box, clears it to white
    // and draws the item into it in black. The original makes that bitmap
    // monochrome; NEO98's text provider draws on 32-bit surfaces only, so the
    // intermediate is a compatible colour bitmap instead. The two raster
    // operations do not care: the intermediate only ever holds black and
    // white, and both operations treat those two the same way at either depth.
    const unsigned mono = Gdi::CreateCompatibleDC(hdc);
    if (!mono) { last_error = 8; return 0; }
    const unsigned arguments[] = {hdc, unsigned(cx + 1), unsigned(cy)};
    const unsigned bitmap = GdiApi("CreateCompatibleBitmap", arguments);
    if (!bitmap) { Gdi::DeleteDC(mono); last_error = 8; return 0; }
    const unsigned keep_bitmap = Gdi::SelectObject(mono, bitmap);
    Gdi::PatBlt(mono, 0, 0, cx + 1, cy, RopWhiteness);     //
    // Windows 98 copies the target's font over, so the image has the right shape.
    const unsigned font = Gdi::GetCurrentObject(hdc, 6);
    const unsigned keep_font = font ? Gdi::SelectObject(mono, font) : 0;
    Gdi::SetTextColor(mono, 0);
    Gdi::SetBkColor(mono, 0xffffff);
    const unsigned drawn = draw(mono, 0, 0);
    // DSS_UNION ORs the checkerboard into the image, which drops every
    // second pixel of it.
    if (flags & StateUnion)
    {
        unsigned pattern = 0;
        if (const unsigned gray = HalftoneBrush(pattern))
        {
            const unsigned keep_brush = Gdi::SelectObject(mono, gray);
            Gdi::PatBlt(mono, 0, 0, cx, cy, RopPatternOr);
            Gdi::SelectObject(mono, keep_brush);
            Gdi::DeleteObject(gray);
            Api("DeleteObject", &pattern);
        }
    }
    bool result = false;
    if (flags & StateDisabled)
    {
        // The highlight one pixel down and right, the
        // shadow on the spot, which is the embossed grey of a disabled item.
        const unsigned hilight = ColorHilight, shadow = ColorShadow;
        result = DrawStateBlit(hdc, Api("GetSysColorBrush", &hilight), mono, x + 1, y + 1, cx, cy);
        result = DrawStateBlit(hdc, Api("GetSysColorBrush", &shadow), mono, x, y, cx, cy) && result;
    }
    else
        result = DrawStateBlit(hdc, brush, mono, x, y, cx, cy);
    if (font) Gdi::SelectObject(mono, keep_font);
    Gdi::SelectObject(mono, keep_bitmap);
    Api("DeleteObject", &bitmap);
    Gdi::DeleteDC(mono);
    return (result && drawn) ? 1u : 0u;
}

// USER is the kernel of TabbedTextOutA and GetTabbedTextExtentA. It
// splits the string at every tab, advances to the next stop and either draws
// the piece or only measures it.
unsigned Application::TabbedTextApi(ApiName name, const unsigned *a)
{
    const bool draw = API_IS(name, "TabbedTextOutA");
    const unsigned hdc = a[0];
    int x = draw ? int(a[1]) : 0, y = draw ? int(a[2]) : 0;
    const unsigned text_pointer = draw ? a[3] : a[1];
    int count = draw ? int(a[4]) : int(a[2]);
    const int stop_count = draw ? int(a[5]) : int(a[3]);
    const unsigned stops = draw ? a[6] : a[4];
    const int origin = draw ? int(a[7]) : 0;
    if (!Gdi::ValidDC(hdc)) { last_error = 1400; return 0; }
    const char *source = ReadString(text_pointer);
    if (!source) { last_error = 87; return 0; }
    if (count < 0) count = int(strlen(source));
    // ReadString hands out a buffer the next string read reuses; the metrics
    // and text calls below can reach code that reads strings (the 16-bit
    // driver of a printer DC), so the text is copied first.
    if (count > 4096) count = 4096;
    char text[4097];
    memcpy(text, source, unsigned(count));
    text[count] = 0;
    Gdi::TEXTMETRICA metrics = {};
    if (!Gdi::GetTextMetricsA(hdc, &metrics)) return 0;
    // USER13 operates on signed WORD coordinates and tab positions. A
    // negative stop right-aligns the following piece, not the preceding one.
    auto word = [](int value) { return int(short(unsigned(value) & 65535)); };
    int fixed = stops ? 0 : word(metrics.tmAveCharWidth * 8);
    int *table = nullptr;
    if (stop_count > 0 && stops)
    {
        if (stop_count > 32767) { last_error = 87; return 0; }
        table = new int[unsigned(stop_count)];
        if (!Read(stops, table, unsigned(stop_count) * 4)) { delete[] table; last_error = 87; return 0; }
        for (int i = 0; i < stop_count; ++i) table[i] = word(table[i]);
        if (stop_count == 1) fixed = table[0] ? table[0] : 1;
    }
    const int start = word(x), tab_origin = word(origin);
    int pen = start, height = metrics.tmHeight;
    int piece = 0;
    bool ok = true, first = true;
    for (int i = 0; i <= count && ok; ++i)
    {
        if (i < count && text[i] != '\t') { ++piece; continue; }
        Gdi::SIZE size = {0, metrics.tmHeight};
        if (piece && !Gdi::GetTextExtentPoint32A(hdc, text + i - piece, piece, &size)) { ok = false; break; }
        size.cx = word(size.cx);
        if (!first)
        {
            if (!fixed)
            {
                int stop = 0;
                for (; table && stop < stop_count; ++stop)
                {
                    const int position = word(tab_origin + (table[stop] < 0 ? -table[stop] - size.cx : table[stop]));
                    if (position > pen) { pen = position; break; }
                }
                // after exhausting the list, use eight average
                // character widths for this and all subsequent tabs.
                if (!table || stop == stop_count) fixed = word(metrics.tmAveCharWidth * 8);
            }
            if (fixed > 0)
                pen = word(tab_origin + (word(pen - tab_origin) / fixed + 1) * fixed);
            else if (fixed < 0)
                pen = word(tab_origin + (word(size.cx - tab_origin + pen) / fixed - 1) * fixed - size.cx);
        }
        first = false;
        if (draw && piece && !Gdi::TextOutA(hdc, pen, y, text + i - piece, piece)) { ok = false; break; }
        pen = word(pen + size.cx);
        height = size.cy;
        piece = 0;
    }
    delete[] table;
    if (!ok) return 0;
    return (unsigned(height) << 16) | (unsigned(pen - start) & 0xffff);
}
} // namespace X86
