// SPDX-License-Identifier: GPL-2.0-or-later
// Printer DC drawing. A 16-bit printer driver owns physical pen, brush and
// font objects rather than a bitmap. Convert DC state and coordinates for
// Output, ExtTextOut and BitBlt, iterating clipping rectangles. Query font
// metrics, text extents and widths from the driver. Native DCs retain
// selection, color, mapping and clipping state; this file supplies their
// drawing path.
#include "x86_print.h"
#include "gdi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
namespace X86
{
using namespace Print;
namespace
{
enum : unsigned
{
    ObjPen = 1, ObjBrush = 2, ObjFont = 3
};
// Output styles.
enum : unsigned
{
    OsRectangle = 6, OsEllipse = 7, OsPolyline = 18, OsAltPolygon = 22, OsWindPolygon = 20,
    OsScanlines = 4, OsBeginNScan = 80, OsEndNScan = 81
};
// dpPolygonalCaps of GDIINFO (+20h).
constexpr unsigned PcPolyPolygon = 0x100;
// The scratch layout of the work block of a printer DC.
constexpr unsigned WorkLogical = 0x000,  // LOGPEN16, LOGBRUSH16 or LOGFONT16
    WorkClip = 0x040,                    // one RECT of 16 bits
    WorkOpaque = 0x048,                  // the opaque RECT of ExtTextOut
    WorkColor = 0x050,                   // ColorInfo's physical colour
    WorkXform = 0x060,                   // TEXTXFORM, 13h bytes
    WorkPoints = 0x100,                  // POINTs of 16 bits, up to 0xF00 bytes
    WorkText = 0x1000,                   // the string of ExtTextOut
    WorkWidths = 0x2000,                 // the widths of ExtTextOut or GetCharWidth
    WorkBytes = 0x3000;
constexpr unsigned MaxPoints = 0xf00 / 4, MaxText = 0x1000;
} // namespace

// The printer half needs a work block for the driver's arguments and a
// DRAWMODE; both live in the 16-bit global heap so the driver can address
// them.
bool Application::PrintWork(PrintDc &dc)
{
    if (!dc.work.selector)
        dc.work = PrintAllocate(WorkBytes);
    if (!dc.drawmode.selector)
        dc.drawmode = PrintAllocate(0x30);
    return dc.work.selector && dc.drawmode.selector;
}

// ColorInfo(lpPDevice, dwColorIn, lpPhysBits): the physical colour of an RGB.
unsigned Application::PrintPhysicalColor(PrintDc &dc, unsigned rgb)
{
    bool ok = false;
    Write32(dc.work.memory + WorkColor, 0);
    const unsigned push[] = {dc.pdevice.selector, 0, rgb >> 16, rgb & 0xffff, dc.work.selector, WorkColor};
    PrintCall(*dc.driver, DdiColorInfo, push, 6, ok);
    return Read32(dc.work.memory + WorkColor);
}

// The DRAWMODE of the DC (DC+4Eh): ROP2, background mode, the physical and
// the logical colours, the break and character extra, the stretch mode.
void Application::PrintDrawMode(PrintDc &dc)
{
    const unsigned hdc = dc.hdc, at = dc.drawmode.memory;
    const unsigned rop[] = {hdc};
    const unsigned text = Gdi::GetTextColor(hdc), back = Gdi::GetBkColor(hdc);
    Win16Word(at + 0x00, GdiApi("GetROP2", rop));
    Win16Word(at + 0x02, unsigned(Gdi::GetBkMode(hdc)));
    Write32(at + 0x04, PrintPhysicalColor(dc, back));
    Write32(at + 0x08, PrintPhysicalColor(dc, text));
    for (unsigned offset = 0x0c; offset < 0x16; offset += 2)
        Win16Word(at + offset, 0);
    Win16Word(at + 0x16, unsigned(Gdi::GetTextCharacterExtra(hdc)));
    Write32(at + 0x18, back);
    Write32(at + 0x1c, text);
    Write32(at + 0x20, 0);
    Win16Word(at + 0x24, GdiApi("GetStretchBltMode", rop));
    Write32(at + 0x26, 0);
}

// A size of logical units in device units, the way GDI scales a pen width or
// a font height with the viewport and window extents.
int Application::PrintDeviceSize(unsigned hdc, int size, bool vertical)
{
    Gdi::RECT r = {0, 0, vertical ? 0 : size, vertical ? size : 0};
    if (!Gdi::DeviceRect(hdc, r))
        return size;
    const int n = vertical ? r.bottom - r.top : r.right - r.left;
    return n < 0 ? -n : n;
}

// RealizeObject(lpPDevice, wStyle, lpInObj, lpOutObj, lpTextXForm).
unsigned Application::PrintRealize(PrintDc &dc, int style, unsigned in_far, unsigned out_far, unsigned xform)
{
    bool ok = false;
    const unsigned push[] = {dc.pdevice.selector, 0, unsigned(style) & 0xffff, in_far >> 16, in_far & 0xffff,
                             out_far >> 16, out_far & 0xffff, xform >> 16, xform & 0xffff};
    const unsigned result = PrintCall(*dc.driver, DdiRealizeObject, push, 9, ok);
    return ok ? result : 0;
}

// The physical pen of the DC, GDI.
bool Application::PrintRealizePen(PrintDc &dc)
{
    const unsigned pen = unsigned(Gdi::GetCurrentObject(dc.hdc, 1));
    const unsigned logical = Allocate(16);
    if (!logical)
        return false;
    const unsigned query[] = {pen, 16, logical};
    GdiApi("GetObjectA", query);
    const unsigned style = Read32(logical), color = Read32(logical + 12);
    const int width = PrintDeviceSize(dc.hdc, int(Read32(logical + 4)), false);
    Free(logical);
    const unsigned key[4] = {pen, style, unsigned(width), color};
    if (dc.pen.selector && !memcmp(key, dc.pen_key, sizeof key))
        return true;
    if (dc.pen.selector)
    {
        PrintRealize(dc, -int(ObjPen), dc.pen.Far(), dc.pen.Far(), dc.pen.Far());
        PrintRelease(dc.pen);
    }
    const unsigned at = dc.work.memory + WorkLogical;
    Win16Word(at, style);
    Win16Word(at + 2, unsigned(width));
    Win16Word(at + 4, 0);
    Write32(at + 6, color);
    const unsigned size = PrintRealize(dc, ObjPen, dc.work.Far(WorkLogical), 0, 0) & 0xffff;
    if (!size)
        return false;
    dc.pen = PrintAllocate(size);
    if (!dc.pen.selector || !PrintRealize(dc, ObjPen, dc.work.Far(WorkLogical), dc.pen.Far(), dc.pen.Far()))
        return false;
    memcpy(dc.pen_key, key, sizeof key);
    return true;
}

// The physical brush of the DC, GDI. The brush origin travels as the
// fifth argument.
bool Application::PrintRealizeBrush(PrintDc &dc)
{
    const unsigned brush = unsigned(Gdi::GetCurrentObject(dc.hdc, 2));
    const unsigned logical = Allocate(16);
    if (!logical)
        return false;
    const unsigned query[] = {brush, 12, logical};
    GdiApi("GetObjectA", query);
    const unsigned style = Read32(logical), color = Read32(logical + 4), hatch = Read32(logical + 8);
    Free(logical);
    if (style == 3 || style == 5 || style == 6)
    {
        Fail("A pattern brush on a printer DC (GDI with a bitmap) is not built yet");
        return false;
    }
    Gdi::POINT origin = {};
    Gdi::GetBrushOrgEx(dc.hdc, &origin);
    const unsigned place = (unsigned(origin.y) & 0xffff) << 16 | (unsigned(origin.x) & 0xffff);
    const unsigned key[5] = {brush, style, color, hatch, place};
    if (dc.brush.selector && !memcmp(key, dc.brush_key, sizeof key))
        return true;
    if (dc.brush.selector)
    {
        PrintRealize(dc, -int(ObjBrush), dc.brush.Far(), dc.brush.Far(), place);
        PrintRelease(dc.brush);
    }
    const unsigned at = dc.work.memory + WorkLogical;
    Win16Word(at, style);
    Write32(at + 2, color);
    Win16Word(at + 6, hatch);
    const unsigned size = PrintRealize(dc, ObjBrush, dc.work.Far(WorkLogical), 0, place) & 0xffff;
    if (!size)
        return false;
    dc.brush = PrintAllocate(size);
    if (!dc.brush.selector || !PrintRealize(dc, ObjBrush, dc.work.Far(WorkLogical), dc.brush.Far(), place))
        return false;
    memcpy(dc.brush_key, key, sizeof key);
    return true;
}

// The physical font of the DC, GDI. The height and the width of the
// LOGFONT go to device units first; the driver fills the TEXTXFORM, and GDI
// makes italic, underline and strikeout 0 or FFh afterwards.
bool Application::PrintRealizeFont(PrintDc &dc)
{
    const unsigned font = unsigned(Gdi::GetCurrentObject(dc.hdc, 6));
    const unsigned logical = Allocate(60);
    if (!logical)
        return false;
    const unsigned query[] = {font, 60, logical};
    GdiApi("GetObjectA", query);
    unsigned char lf[60] = {};
    Read(logical, lf, sizeof lf);
    Free(logical);
    int height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
    memcpy(&height, lf, 4);
    memcpy(&width, lf + 4, 4);
    memcpy(&escapement, lf + 8, 4);
    memcpy(&orientation, lf + 12, 4);
    memcpy(&weight, lf + 16, 4);
    const int device_height = height < 0 ? -PrintDeviceSize(dc.hdc, -height, true)
                                         : PrintDeviceSize(dc.hdc, height, true);
    const int device_width = PrintDeviceSize(dc.hdc, width < 0 ? -width : width, false);
    // The key: the handle, the device sizes and the rest of the LOGFONT.
    unsigned key[16] = {font, unsigned(device_height), unsigned(device_width)};
    memcpy(key + 3, lf + 8, 52);
    if (dc.font.selector && !memcmp(key, dc.font_key, sizeof key))
        return true;
    if (dc.font.selector)
    {
        PrintRealize(dc, -int(ObjFont), dc.font.Far(), dc.font.Far(), dc.xform.Far());
        PrintRelease(dc.font);
    }
    const unsigned at = dc.work.memory + WorkLogical;
    Win16Word(at, unsigned(device_height));
    Win16Word(at + 2, unsigned(device_width));
    Win16Word(at + 4, unsigned(escapement));
    Win16Word(at + 6, unsigned(orientation));
    Win16Word(at + 8, unsigned(weight));
    Store(at + 10, lf + 20, 8);  // italic to pitch and family
    Store(at + 18, lf + 28, 32); // the face name
    const unsigned size = PrintRealize(dc, ObjFont, dc.work.Far(WorkLogical), 0, 0) & 0xffff;
    if (!size)
        return false;
    if (!dc.xform.selector)
        dc.xform = PrintAllocate(0x20);
    dc.font = PrintAllocate(size + 0x42);
    if (!dc.font.selector || !dc.xform.selector ||
        !PrintRealize(dc, ObjFont, dc.work.Far(WorkLogical), dc.font.Far(), dc.xform.Far()))
        return false;
    for (unsigned flag = 0x0a; flag <= 0x0c; ++flag)
    {
        unsigned char b = 0;
        Read(dc.xform.memory + flag, &b, 1);
        b = b ? 0xff : 0;
        Store(dc.xform.memory + flag, &b, 1);
    }
    memcpy(dc.font_key, key, sizeof key);
    return true;
}

// Everything a drawing call needs: the work block, the DRAWMODE and the
// three physical objects.
bool Application::PrintPrepare(PrintDc &dc, bool pen, bool brush, bool font)
{
    if (!PrintWork(dc))
        return false;
    PrintDrawMode(dc);
    if (pen && !PrintRealizePen(dc))
        return false;
    if (brush && !PrintRealizeBrush(dc))
        return false;
    if (font && !PrintRealizeFont(dc))
        return false;
    return true;
}

// The clip region of the DC in device units, one rectangle after the other,
// as and enumerate it. Answers the number written.
unsigned Application::PrintClipRects(PrintDc &dc, Gdi::RECT *rects, unsigned capacity)
{
    const auto region = Gdi::ClipRegionObject(dc.hdc);
    if (!region)
        return 0;
    const unsigned count = Gdi::RegionRectangles(region, rects, capacity, 0, 0);
    Gdi::DeleteObject(region);
    return count < capacity ? count : capacity;
}

// Output(lpPDevice, wStyle, wCount, lpPoints, lpPPen, lpPBrush, lpDrawMode,
// lpClipRect), DDI ordinal 8, once per clip rectangle.
bool Application::PrintOutput(PrintDc &dc, unsigned style, const Gdi::POINT *points, unsigned count, bool pen,
                              bool brush)
{
    if (count > MaxPoints)
    {
        Fail("A printer Output call with more points than the work block holds");
        return false;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        Win16Word(dc.work.memory + WorkPoints + i * 4, unsigned(points[i].x));
        Win16Word(dc.work.memory + WorkPoints + i * 4 + 2, unsigned(points[i].y));
    }
    Gdi::RECT clips[64];
    const unsigned rects = PrintClipRects(dc, clips, 64);
    const unsigned pen_far = pen ? dc.pen.Far() : 0, brush_far = brush ? dc.brush.Far() : 0;
    bool drawn = false;
    for (unsigned i = 0; i < rects; ++i)
    {
        const unsigned clip = dc.work.memory + WorkClip;
        Win16Word(clip, unsigned(clips[i].left));
        Win16Word(clip + 2, unsigned(clips[i].top));
        Win16Word(clip + 4, unsigned(clips[i].right));
        Win16Word(clip + 6, unsigned(clips[i].bottom));
        bool ok = false;
        const unsigned pts = dc.work.Far(WorkPoints), mode = dc.drawmode.Far(), clip_far = dc.work.Far(WorkClip);
        const unsigned push[] = {dc.pdevice.selector, 0, style, count, pts >> 16, pts & 0xffff,
                                 pen_far >> 16, pen_far & 0xffff, brush_far >> 16, brush_far & 0xffff,
                                 mode >> 16, mode & 0xffff, clip_far >> 16, clip_far & 0xffff};
        const int result = short(PrintCall(*dc.driver, DdiOutput, push, 14, ok) & 0xffff);
        if (!ok)
            return false;
        drawn |= result > 0;
    }
    return drawn || !rects;
}

// Logical points of the guest to device points.
bool Application::PrintDevicePoints(unsigned hdc, const unsigned points, unsigned count, Gdi::POINT *out)
{
    for (unsigned i = 0; i < count; ++i)
    {
        Gdi::RECT r = {int(Read32(points + i * 8)), int(Read32(points + i * 8 + 4)), 0, 0};
        r.right = r.left;
        r.bottom = r.top;
        if (!Gdi::DeviceRect(hdc, r))
            return false;
        out[i] = {r.left, r.top};
    }
    return true;
}

// ExtTextOut(lpPDevice, x, y, lpClipRect, lpString, wCount, lpFontInfo,
// lpDrawMode, lpTextXForm, lpCharWidths, lpOpaqueRect, wOptions), DDI 14.
unsigned Application::PrintExtTextOut(PrintDc &dc, int x, int y, unsigned clip_far, unsigned count, bool measure,
                                      unsigned widths_far, unsigned opaque_far, unsigned options)
{
    bool ok = false;
    const unsigned text = dc.work.Far(WorkText), font = dc.font.Far(), mode = dc.drawmode.Far(),
                   xform = dc.xform.Far();
    const unsigned signed_count = measure ? (0x10000u - count) & 0xffff : count;
    const unsigned push[] = {dc.pdevice.selector, 0, unsigned(x) & 0xffff, unsigned(y) & 0xffff,
                             clip_far >> 16, clip_far & 0xffff, text >> 16, text & 0xffff, signed_count,
                             font >> 16, font & 0xffff, mode >> 16, mode & 0xffff, xform >> 16, xform & 0xffff,
                             widths_far >> 16, widths_far & 0xffff, opaque_far >> 16, opaque_far & 0xffff,
                             options & 0xffff};
    const unsigned result = PrintCall(*dc.driver, DdiExtTextOut, push, 20, ok);
    return ok ? result : 0;
}

// The extent of a string from the driver, GDI.
bool Application::PrintTextExtent(PrintDc &dc, const char *text, unsigned count, int &cx, int &cy)
{
    if (count > MaxText)
        count = MaxText;
    Store(dc.work.memory + WorkText, text, count);
    const unsigned extent = PrintExtTextOut(dc, 0, 0, 0, count, true, 0, 0, 0);
    cx = int(extent & 0xffff);
    cy = int(extent >> 16);
    return true;
}

// FONTINFO+8 dfAscent and its neighbours, GDI's GetTextMetrics of a device
// font.
bool Application::PrintTextMetrics(PrintDc &dc, Gdi::TEXTMETRICA &tm)
{
    unsigned char fi[0x30] = {}, xf[0x14] = {};
    Read(dc.font.memory, fi, sizeof fi);
    Read(dc.xform.memory, xf, sizeof xf);
    auto word = [&](unsigned at) { return int(short(fi[at] | fi[at + 1] << 8)); };
    tm = {};
    tm.tmHeight = word(0x16);
    tm.tmAscent = word(0x08);
    tm.tmDescent = tm.tmHeight - tm.tmAscent;
    tm.tmInternalLeading = word(0x0a);
    tm.tmExternalLeading = word(0x0c);
    tm.tmAveCharWidth = word(0x19);
    tm.tmMaxCharWidth = word(0x1b);
    tm.tmWeight = word(0x11);
    tm.tmOverhang = int(short(xf[0x11] | xf[0x12] << 8));
    tm.tmDigitizedAspectX = word(0x06);
    tm.tmDigitizedAspectY = word(0x04);
    tm.tmFirstChar = fi[0x1d];
    tm.tmLastChar = fi[0x1e];
    tm.tmDefaultChar = (unsigned char)(fi[0x1f] + fi[0x1d]);
    tm.tmBreakChar = (unsigned char)(fi[0x20] + fi[0x1d]);
    tm.tmItalic = xf[0x0a];
    tm.tmUnderlined = xf[0x0b];
    tm.tmStruckOut = xf[0x0c];
    // A device font carries TMPF_DEVICE.
    tm.tmPitchAndFamily = (unsigned char)(fi[0x18] | ((fi[0] & 0x80) ? 0x08 : 0));
    tm.tmCharSet = fi[0x13];
    return true;
}

// The logical size of a device size, DPtoLP over a rectangle from the origin.
static int LogicalSize(unsigned hdc, int size, bool vertical)
{
    Gdi::RECT r = {0, 0, vertical ? 0 : size, vertical ? size : 0};
    if (!Gdi::LogicalRect(hdc, r))
        return size;
    const int n = vertical ? r.bottom - r.top : r.right - r.left;
    return n < 0 ? -n : n;
}

// ExtTextOut on a printer DC, GDI: the reference point with the text
// alignment of the DC applied, the widths in device units, once per clip
// rectangle.
bool Application::PrinterTextOut(PrintDc &dc, int x0, int y0, unsigned options, const Gdi::RECT *rect,
                                 const char *text, unsigned count, const int *dx)
{
    if (dc.information)
        return false;
    if (count > MaxText)
        count = MaxText;
    // The caller's text may sit in a buffer of this port that the driver's
    // own gate calls reuse (ReadString); it is copied before the driver runs.
    char copy[MaxText];
    if (count)
        memcpy(copy, text, count);
    text = copy;
    int advances[MaxText];
    if (dx && count)
    {
        memcpy(advances, dx, count * sizeof *dx);
        dx = advances;
    }
    if (!PrintPrepare(dc, false, false, true))
        return false;
    const unsigned hdc = dc.hdc;
    const unsigned align = Gdi::GetTextAlign(hdc);
    Gdi::RECT at = {x0, y0, 0, 0};
    if (align & 1)
    {
        Gdi::POINT cp = {};
        Gdi::CurrentPosition(hdc, cp);
        at.left = cp.x;
        at.top = cp.y;
    }
    at.right = at.left;
    at.bottom = at.top;
    Gdi::DeviceRect(hdc, at);
    int x = at.left, y = at.top;
    unsigned widths_far = 0;
    int total = 0;
    if (dx && count)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            const int w = PrintDeviceSize(hdc, dx[i], false);
            Win16Word(dc.work.memory + WorkWidths + i * 2, unsigned(w));
            total += w;
        }
        widths_far = dc.work.Far(WorkWidths);
    }
    int cx = total, cy = 0;
    if (!dx || (align & 6))
    {
        int ex = 0;
        PrintTextExtent(dc, text, count, ex, cy);
        if (!dx)
            cx = ex;
    }
    Store(dc.work.memory + WorkText, text, count);
    Gdi::TEXTMETRICA tm;
    PrintTextMetrics(dc, tm);
    if ((align & 6) == 6)
        x -= cx / 2;
    else if (align & 2)
        x -= cx;
    if ((align & 0x18) == 0x18)
        y -= tm.tmAscent;
    else if (align & 8)
        y -= tm.tmHeight;
    unsigned opaque_far = 0;
    if (rect && (options & 6))
    {
        Gdi::RECT r = *rect;
        Gdi::DeviceRect(hdc, r);
        const unsigned o = dc.work.memory + WorkOpaque;
        Win16Word(o, unsigned(r.left));
        Win16Word(o + 2, unsigned(r.top));
        Win16Word(o + 4, unsigned(r.right));
        Win16Word(o + 6, unsigned(r.bottom));
        opaque_far = dc.work.Far(WorkOpaque);
    }
    Gdi::RECT clips[64];
    const unsigned rects = PrintClipRects(dc, clips, 64);
    for (unsigned i = 0; i < rects; ++i)
    {
        const unsigned clip = dc.work.memory + WorkClip;
        Win16Word(clip, unsigned(clips[i].left));
        Win16Word(clip + 2, unsigned(clips[i].top));
        Win16Word(clip + 4, unsigned(clips[i].right));
        Win16Word(clip + 6, unsigned(clips[i].bottom));
        PrintExtTextOut(dc, x, y, dc.work.Far(WorkClip), count, false, widths_far, opaque_far, options);
    }
    if (align & 1)
    {
        Gdi::POINT cp = {};
        Gdi::CurrentPosition(hdc, cp);
        const unsigned move[] = {hdc, unsigned(cp.x + LogicalSize(hdc, cx, false)), unsigned(cp.y), 0};
        GdiApi("MoveToEx", move);
    }
    return true;
}

// GetTextMetrics of a printer DC in logical units.
bool Application::PrinterTextMetricsLogical(PrintDc &dc, Gdi::TEXTMETRICA &tm)
{
    if (!PrintPrepare(dc, false, false, true))
        return false;
    PrintTextMetrics(dc, tm);
    const unsigned hdc = dc.hdc;
    tm.tmHeight = LogicalSize(hdc, tm.tmHeight, true);
    tm.tmAscent = LogicalSize(hdc, tm.tmAscent, true);
    tm.tmDescent = LogicalSize(hdc, tm.tmDescent, true);
    tm.tmInternalLeading = LogicalSize(hdc, tm.tmInternalLeading, true);
    tm.tmExternalLeading = LogicalSize(hdc, tm.tmExternalLeading, true);
    tm.tmAveCharWidth = LogicalSize(hdc, tm.tmAveCharWidth, false);
    tm.tmMaxCharWidth = LogicalSize(hdc, tm.tmMaxCharWidth, false);
    tm.tmOverhang = LogicalSize(hdc, tm.tmOverhang, false);
    return true;
}

// GetTextExtentPoint of a printer DC in logical units.
bool Application::PrinterTextExtentLogical(PrintDc &dc, const char *text, unsigned count, Gdi::SIZE &size)
{
    if (count > MaxText)
        count = MaxText;
    char copy[MaxText];
    if (count)
        memcpy(copy, text, count);
    text = copy;
    if (!PrintPrepare(dc, false, false, true))
        return false;
    int cx = 0, cy = 0;
    PrintTextExtent(dc, text, count, cx, cy);
    size = {LogicalSize(dc.hdc, cx, false), LogicalSize(dc.hdc, cy, true)};
    return true;
}

// GetCharWidth(lpPDevice, lpBuffer, wFirst, wLast, lpFontInfo, lpDrawMode,
// lpTextXForm), DDI 15, in logical units.
bool Application::PrinterCharWidths(PrintDc &dc, unsigned first, unsigned last, int *widths)
{
    if (!PrintPrepare(dc, false, false, true) || last < first || last > 255)
        return false;
    bool ok = false;
    const unsigned buffer = dc.work.Far(WorkWidths), font = dc.font.Far(), mode = dc.drawmode.Far(),
                   xform = dc.xform.Far();
    const unsigned push[] = {dc.pdevice.selector, 0, buffer >> 16, buffer & 0xffff, first, last,
                             font >> 16, font & 0xffff, mode >> 16, mode & 0xffff, xform >> 16, xform & 0xffff};
    const unsigned answer = PrintCall(*dc.driver, DdiGetCharWidth, push, 12, ok) & 0xffff;
    if (!ok || !answer)
        return false;
    for (unsigned i = 0; i <= last - first; ++i)
        widths[i] = LogicalSize(dc.hdc, int(short(Win16Word(dc.work.memory + WorkWidths + i * 2))), false);
    return true;
}

// PolyPolygon on a printer DC, GDI and its worker. A
// driver with PC_POLYPOLYGON in dpPolygonalCaps would take the polygons in
// one Output; PSCRIPT does not have it (its caps are 00FFh), so GDI draws the
// interior itself: turns the polygons, cut by the clip region, into
// scan lines and hands each to Output(OS_SCANLINES) with the brush and no
// pen, between OS_BEGINNSCAN and OS_ENDNSCAN.
// (A driver of the DIB engine's kind takes whole bands with style 5 instead; PSCRIPT answers -1 to that.) The outline is then one Polyline per
// polygon with the pen. Here the segments come from the
// native region of the polygons in device units.
bool Application::PrinterPolyPolygon(PrintDc &dc, unsigned points, unsigned counts, unsigned polygons,
                                     unsigned &result)
{
    result = 0;
    if (dc.information || !polygons)
        return true;
    unsigned total = 0;
    for (unsigned i = 0; i < polygons; ++i)
        total += Read32(counts + i * 4);
    if (!total || total > 0x3ffd)
        return true;
    Gdi::POINT *device = static_cast<Gdi::POINT *>(malloc(total * sizeof(Gdi::POINT)));
    if (!device)
        return true;
    struct Release
    {
        Gdi::POINT *p;
        ~Release() { free(p); }
    } release{device};
    if (!PrintDevicePoints(dc.hdc, points, total, device))
        return true;
    unsigned caps = 0;
    Read(dc.gdiinfo.memory + 0x20, &caps, 2);
    if (caps & PcPolyPolygon)
    {
        Fail("PolyPolygon on a printer with PC_POLYPOLYGON (one Output, GDI) is not built yet");
        return true;
    }
    const unsigned hdc = dc.hdc;
    const unsigned scratch = Allocate(total * 8 + polygons * 4 + 16);
    if (!scratch)
        return true;
    // BS_NULL leaves the interior out, PS_NULL the outline.
    unsigned brush_style = 1, pen_style = 5;
    {
        const unsigned brush[] = {unsigned(Gdi::GetCurrentObject(hdc, 2)), 12, scratch};
        if (GdiApi("GetObjectA", brush))
            brush_style = Read32(scratch);
        const unsigned pen[] = {unsigned(Gdi::GetCurrentObject(hdc, 1)), 16, scratch};
        if (GdiApi("GetObjectA", pen))
            pen_style = Read32(scratch);
    }
    bool drawn = false;
    if (brush_style != 1 && PrintPrepare(dc, false, true, false))
    {
        for (unsigned i = 0; i < total; ++i)
        {
            Write32(scratch + i * 8, unsigned(device[i].x));
            Write32(scratch + i * 8 + 4, unsigned(device[i].y));
        }
        for (unsigned i = 0; i < polygons; ++i)
            Write32(scratch + total * 8 + i * 4, Read32(counts + i * 4));
        const unsigned mode_query[] = {hdc};
        const unsigned make[] = {scratch, scratch + total * 8, polygons, GdiApi("GetPolyFillMode", mode_query)};
        const unsigned shape = GdiApi("CreatePolyPolygonRgn", make);
        const auto clip = Gdi::ClipRegionObject(hdc);
        if (shape && clip)
            Gdi::CombineRegions(Gdi::HGDIOBJ(shape), Gdi::HGDIOBJ(shape), clip, 1);
        if (clip)
            Gdi::DeleteObject(clip);
        unsigned rect_count = shape ? Gdi::RegionRectangles(Gdi::HGDIOBJ(shape), nullptr, 0, 0, 0) : 0;
        Gdi::RECT *rects = rect_count ? static_cast<Gdi::RECT *>(malloc(rect_count * sizeof(Gdi::RECT))) : nullptr;
        if (rects)
            rect_count = Gdi::RegionRectangles(Gdi::HGDIOBJ(shape), rects, rect_count, 0, 0);
        if (shape)
        {
            const unsigned dead[] = {shape};
            GdiApi("DeleteObject", dead);
        }
        const unsigned brush_far = dc.brush.Far(), mode = dc.drawmode.Far();
        auto output = [&](unsigned style, unsigned count, unsigned points_far) {
            bool ok = false;
            const unsigned push[] = {dc.pdevice.selector, 0, style, count, points_far >> 16, points_far & 0xffff,
                                     0, 0, brush_far >> 16, brush_far & 0xffff, mode >> 16, mode & 0xffff, 0, 0};
            const int answer = short(PrintCall(*dc.driver, DdiOutput, push, 14, ok) & 0xffff);
            return ok ? answer : 0;
        };
        if (rects && rect_count)
        {
            output(OsBeginNScan, 0, 0);
            // one scan line per call, the first POINT (0, y), then
            // one POINT (left, right) per segment, wCount the segments plus one.
            const unsigned base = dc.work.memory + WorkPoints;
            for (unsigned i = 0; i < rect_count;)
            {
                unsigned j = i;
                while (j < rect_count && rects[j].top == rects[i].top && rects[j].bottom == rects[i].bottom)
                    ++j;
                const unsigned pairs = j - i < MaxPoints - 1 ? j - i : MaxPoints - 1;
                for (unsigned k = 0; k < pairs; ++k)
                {
                    Win16Word(base + 4 + k * 4, unsigned(rects[i + k].left));
                    Win16Word(base + 6 + k * 4, unsigned(rects[i + k].right));
                }
                for (int y = rects[i].top; y < rects[i].bottom; ++y)
                {
                    Win16Word(base, 0);
                    Win16Word(base + 2, unsigned(y));
                    if (output(OsScanlines, pairs + 1, dc.work.Far(WorkPoints)) > 0)
                        drawn = true;
                }
                i = j;
            }
            output(OsEndNScan, 0, 0);
        }
        free(rects);
    }
    if (pen_style != 5 && PrintPrepare(dc, true, false, false))
    {
        unsigned first = 0;
        for (unsigned i = 0; i < polygons; ++i)
        {
            const unsigned n = Read32(counts + i * 4);
            if (n >= 2 && n < MaxPoints)
            {
                Gdi::POINT ring[MaxPoints];
                memcpy(ring, device + first, n * sizeof *ring);
                ring[n] = device[first];
                drawn |= PrintOutput(dc, OsPolyline, ring, n + 1, true, false);
            }
            first += n;
        }
    }
    Free(scratch);
    result = drawn ? 1 : 0;
    return true;
}

// PatBlt on a printer DC: the driver's BitBlt without a source (DDI 1),
// BitBlt(lpDestDev, wDestX, wDestY, lpSrcDev, wSrcX, wSrcY, wXext, wYext,
// dwRop3, lpPBrush, lpDrawMode), once per rectangle the clip leaves of it.
bool Application::PrinterPatBlt(PrintDc &dc, int x, int y, int width, int height, unsigned rop)
{
    if (dc.information)
        return false;
    if (!PrintPrepare(dc, false, true, false))
        return false;
    Gdi::RECT r = {x, y, x + width, y + height};
    Gdi::DeviceRect(dc.hdc, r);
    if (r.left > r.right)
    {
        const int t = r.left;
        r.left = r.right;
        r.right = t;
    }
    if (r.top > r.bottom)
    {
        const int t = r.top;
        r.top = r.bottom;
        r.bottom = t;
    }
    Gdi::RECT clips[64];
    const unsigned rects = PrintClipRects(dc, clips, 64);
    for (unsigned i = 0; i < rects; ++i)
    {
        const int left = r.left > clips[i].left ? r.left : clips[i].left;
        const int top = r.top > clips[i].top ? r.top : clips[i].top;
        const int right = r.right < clips[i].right ? r.right : clips[i].right;
        const int bottom = r.bottom < clips[i].bottom ? r.bottom : clips[i].bottom;
        if (left >= right || top >= bottom)
            continue;
        bool ok = false;
        const unsigned brush = dc.brush.Far(), mode = dc.drawmode.Far();
        const unsigned push[] = {dc.pdevice.selector, 0, unsigned(left) & 0xffff, unsigned(top) & 0xffff, 0, 0, 0, 0,
                                 unsigned(right - left), unsigned(bottom - top), rop >> 16, rop & 0xffff,
                                 brush >> 16, brush & 0xffff, mode >> 16, mode & 0xffff};
        PrintCall(*dc.driver, DdiBitBlt, push, 16, ok);
        if (!ok)
            return false;
    }
    return true;
}

// The hooks of src/gdi.h: every caller of the native text and fill routines,
// USER as much as GDI32, reaches the driver.
bool Application::PrintHookTextOut(void *context, unsigned, int x, int y, unsigned options, const Gdi::RECT *rect,
                                   const char *text, unsigned count, const int *dx)
{
    auto *dc = static_cast<PrintDc *>(context);
    return dc->owner->PrinterTextOut(*dc, x, y, options, rect, text, count, dx);
}
bool Application::PrintHookMetrics(void *context, unsigned, Gdi::TEXTMETRICA *tm)
{
    auto *dc = static_cast<PrintDc *>(context);
    return tm && dc->owner->PrinterTextMetricsLogical(*dc, *tm);
}
bool Application::PrintHookExtent(void *context, unsigned, const char *text, unsigned count, Gdi::SIZE *size)
{
    auto *dc = static_cast<PrintDc *>(context);
    return size && dc->owner->PrinterTextExtentLogical(*dc, text, count, *size);
}
bool Application::PrintHookWidths(void *context, unsigned, unsigned first, unsigned last, int *widths)
{
    auto *dc = static_cast<PrintDc *>(context);
    return dc->owner->PrinterCharWidths(*dc, first, last, widths);
}
bool Application::PrintHookPatBlt(void *context, unsigned, int x, int y, int width, int height, unsigned rop)
{
    auto *dc = static_cast<PrintDc *>(context);
    return dc->owner->PrinterPatBlt(*dc, x, y, width, height, rop);
}
void Application::PrintInstallHooks(PrintDc &dc)
{
    static const Gdi::DeviceDraw hooks = {PrintHookTextOut, PrintHookMetrics, PrintHookExtent, PrintHookWidths,
                                          PrintHookPatBlt};
    Gdi::SetDeviceDraw(dc.hdc, &hooks);
}

// EnumDFonts(lpDestDev, lpFaceName, lpCallbackFunc, lpClientData), DDI 6,
// as GDI calls it for a driver with RC_GDI20_OUTPUT. The callback is
// GDI's own, here the private gate PrintEnumOrdinal of the GDI
// module; it turns every device font into a record of the shared
// enumeration (src/x86_font_query.cpp). Answers the number of records.
unsigned Application::PrintEnumDeviceFonts(unsigned hdc, const char *face, unsigned char *rows, unsigned capacity)
{
    PrintDc *dc = PrintFind(hdc);
    if (!dc || !PrintWork(*dc) || !Win16HostedReady())
        return 0;
    unsigned char info[0x28] = {};
    Read(dc->gdiinfo.memory, info, sizeof info);
    if (!((info[0x26] | info[0x27] << 8) & 0x10))
        return 0;
    if (!print_enum_gate)
    {
        const unsigned gate = Win16Gate("GDI", "EnumDFontsCallback", PrintEnumOrdinal);
        print_enum_gate = gate ? Win16GatePointer(gate) : 0;
        if (!print_enum_gate)
            return 0;
    }
    unsigned face_far = 0;
    if (face && *face)
    {
        PutString(dc->work.memory + WorkText, face);
        face_far = dc->work.Far(WorkText);
    }
    PrintEnumSink sink{dc, rows, capacity, 0};
    PrintEnumSink *outer = print_enum;
    print_enum = &sink;
    bool ok = false;
    const unsigned push[] = {dc->pdevice.selector, 0, face_far >> 16, face_far & 0xffff, print_enum_gate >> 16,
                             print_enum_gate & 0xffff, 0, 0};
    PrintCall(*dc->driver, DdiEnumDFonts, push, 8, ok);
    print_enum = outer;
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "PRINT EnumDFonts face=\"%s\" fonts=%u", face ? face : "", sink.count);
        log(text);
    }
    return sink.count;
}

// GDI for one device font: callback(lpLogFont, lpTextMetrics,
// nFontType, lpData), Pascal, 7 words. The sizes go to logical units, the type gets DEVICE_FONTTYPE unless it is TrueType,
// and tmPitchAndFamily keeps its pitch and family bits with TMPF_DEVICE.
unsigned Application::PrintEnumCallback(const Win16Frame &frame)
{
    PrintEnumSink *sink = print_enum;
    if (!sink || sink->count >= sink->capacity)
        return 0;
    const unsigned logfont = frame.Pointer(5), metrics = frame.Pointer(3);
    unsigned type = frame.Word(2);
    if (!logfont || !metrics)
        return 1;
    const unsigned hdc = sink->dc->hdc;
    unsigned char *row = sink->rows + sink->count * 264;
    memset(row, 0, 264);
    auto put = [&](unsigned at, int value) { memcpy(row + at, &value, 4); };
    auto word = [&](unsigned at) { return int(short(Win16Word(at))); };
    put(0, word(logfont) < 0 ? -LogicalSize(hdc, -word(logfont), true) : LogicalSize(hdc, word(logfont), true));
    put(4, LogicalSize(hdc, word(logfont + 2), false));
    put(8, word(logfont + 4));
    put(12, word(logfont + 6));
    put(16, word(logfont + 8));
    Read(logfont + 10, row + 20, 8 + 32); // italic to pitch, then the face
    row[27 + 32] = 0;
    // ENUMLOGFONT: the full name is the face.
    memcpy(row + 60, row + 28, 32);
    Gdi::TEXTMETRICA tm = {};
    const int heights[] = {word(metrics), word(metrics + 2), word(metrics + 4), word(metrics + 6), word(metrics + 8)};
    tm.tmHeight = LogicalSize(hdc, heights[0], true);
    tm.tmAscent = LogicalSize(hdc, heights[1], true);
    tm.tmDescent = LogicalSize(hdc, heights[2], true);
    tm.tmInternalLeading = LogicalSize(hdc, heights[3], true);
    tm.tmExternalLeading = LogicalSize(hdc, heights[4], true);
    tm.tmAveCharWidth = LogicalSize(hdc, word(metrics + 10), false);
    tm.tmMaxCharWidth = LogicalSize(hdc, word(metrics + 12), false);
    tm.tmWeight = word(metrics + 14);
    tm.tmOverhang = LogicalSize(hdc, word(metrics + 16), false);
    tm.tmDigitizedAspectX = word(metrics + 18);
    tm.tmDigitizedAspectY = word(metrics + 20);
    Read(metrics + 22, &tm.tmFirstChar, 9);
    if (!(type & 4))
    {
        type |= 2;
        tm.tmPitchAndFamily = (unsigned char)((tm.tmPitchAndFamily & 0xf1) | 8);
    }
    memcpy(row + 188, &tm, sizeof tm);
    put(260, int(type));
    ++sink->count;
    return 1;
}

// The drawing and text calls of GDI32 on a printer DC. Answers false when the
// call is not one of them.
bool Application::PrinterDrawApi(ApiName name, const unsigned *a, unsigned &result)
{
    PrintDc *dc = PrintFind(a[0]);
    if (!dc)
        return false;
    result = 0;
    const unsigned hdc = a[0];
    if (API_IS(name, "Rectangle") || API_IS(name, "Ellipse"))
    {
        if (dc->information || !PrintPrepare(*dc, true, true, false))
            return true;
        Gdi::RECT r = {int(a[1]), int(a[2]), int(a[3]), int(a[4])};
        Gdi::DeviceRect(hdc, r);
        const Gdi::POINT corners[2] = {{r.left < r.right ? r.left : r.right, r.top < r.bottom ? r.top : r.bottom},
                                       {r.left < r.right ? r.right : r.left, r.top < r.bottom ? r.bottom : r.top}};
        result = PrintOutput(*dc, API_IS(name, "Rectangle") ? OsRectangle : OsEllipse, corners, 2, true, true);
        return true;
    }
    if (API_IS(name, "LineTo"))
    {
        if (dc->information || !PrintPrepare(*dc, true, false, false))
            return true;
        Gdi::POINT from = {};
        Gdi::CurrentPosition(hdc, from);
        Gdi::RECT r = {from.x, from.y, int(a[1]), int(a[2])};
        Gdi::DeviceRect(hdc, r);
        const Gdi::POINT line[2] = {{r.left, r.top}, {r.right, r.bottom}};
        result = PrintOutput(*dc, OsPolyline, line, 2, true, false);
        const unsigned move[] = {hdc, a[1], a[2], 0};
        GdiApi("MoveToEx", move);
        return true;
    }
    if (API_IS(name, "Polyline") || API_IS(name, "Polygon"))
    {
        if (dc->information || !PrintPrepare(*dc, true, API_IS(name, "Polygon"), false))
            return true;
        const unsigned count = a[2];
        if (count > MaxPoints)
        {
            Fail("A printer Polyline or Polygon with more points than the work block holds");
            return true;
        }
        Gdi::POINT points[MaxPoints];
        if (!PrintDevicePoints(hdc, a[1], count, points))
            return true;
        const unsigned query[] = {hdc};
        const unsigned fill = GdiApi("GetPolyFillMode", query);
        result = PrintOutput(*dc, API_IS(name, "Polyline") ? OsPolyline : fill == 2 ? OsWindPolygon : OsAltPolygon,
                             points, count, true, API_IS(name, "Polygon"));
        return true;
    }
    if (API_IS(name, "GetTextFaceA"))
    {
        if (!PrintPrepare(*dc, false, false, true))
            return true;
        unsigned char fi[0x2c] = {};
        Read(dc->font.memory, fi, sizeof fi);
        const unsigned face = fi[0x27] | fi[0x28] << 8;
        char name_text[64] = {};
        Read(dc->font.memory + face, name_text, sizeof name_text - 1);
        const unsigned length = unsigned(strlen(name_text));
        if (!a[2])
        {
            result = length + 1;
            return true;
        }
        const unsigned n = a[1] ? (length + 1 < a[1] ? length + 1 : a[1]) : 0;
        if (n)
        {
            Store(a[2], name_text, n - 1);
            const char zero = 0;
            Store(a[2] + n - 1, &zero, 1);
        }
        result = n ? n - 1 : 0;
        return true;
    }
    if (API_IS(name, "PolyPolygon"))
        return PrinterPolyPolygon(*dc, a[1], a[2], a[3], result);
    static const char *const unbuilt[] = {"BitBlt", "StretchBlt", "StretchDIBits", "SetDIBitsToDevice",
                                          "RoundRect", "Arc", "Pie",
                                          "Chord", "PolyBezier", "FillRgn", "PaintRgn",
                                          "FrameRgn", "SetPixel", "SetPixelV", "FloodFill", "ExtFloodFill",
                                          "PolylineTo", "PolyBezierTo", "PolyPolyline", "StrokePath",
                                          "FillPath", "StrokeAndFillPath", "DrawEscape"};
    for (const char *entry : unbuilt)
        if (!strcmp(entry, name.text))
        {
            char text[160];
            snprintf(text, sizeof text, "%s on a printer DC is not built yet", entry);
            Fail(text);
            return true;
        }
    return false;
}

// The physical objects go with the DC.
void Application::PrintReleaseObjects(PrintDc &dc)
{
    if (dc.pen.selector)
        PrintRealize(dc, -int(ObjPen), dc.pen.Far(), dc.pen.Far(), dc.pen.Far());
    if (dc.brush.selector)
        PrintRealize(dc, -int(ObjBrush), dc.brush.Far(), dc.brush.Far(), 0);
    if (dc.font.selector)
        PrintRealize(dc, -int(ObjFont), dc.font.Far(), dc.font.Far(), dc.xform.Far());
    PrintRelease(dc.pen);
    PrintRelease(dc.brush);
    PrintRelease(dc.font);
    PrintRelease(dc.xform);
    PrintRelease(dc.work);
    PrintRelease(dc.drawmode);
}
} // namespace X86
