// SPDX-License-Identifier: GPL-2.0-or-later
// The GDI32 exports that are 32-to-16 gates into GDI.EXE and were still
// missing from the export contract on 23 September 2026 (branch dlls, phase
// 2). Each entry names the GDI32 stub and the GDI.EXE routine it follows.
// Two gates recur. The normal gate accepts the DC signatures 4F4Dh to 4F50h,
// the exact gate only 4F4Dh, which is a screen, memory or EMF DC; a WMF DC
// fails it. A failed gate reports through KERNEL.327 and leaves through the
// error exit of its entry; the port sets LastError 6 there, as it does for the
// other gates of GDI.
#include "x86_app.h"
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned EmrSetMetaRgn = 0x1c, EmrSetMiterLimit = 0x3a, EmrGdiComment = 0x46;
unsigned FloatOne() { return 0x3f800000u; }
} // namespace

// 0 no metafile, 1 a WMF DC, 2 an EMF DC.
unsigned Application::GdiDcKind(unsigned dc)
{
    return MetafileKindOf(dc);
}

unsigned Application::Gdi32GateApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // CL 79h, over. Nothing but the normal gate and
    // the probe of the answer buffer happens before the ICM bit of DC+B6h is
    // tested, and SetICMMode never succeeds in this port: FALSE, LastError
    // untouched.
    API_CASE("CheckColorsInGamut")
    {
        if (!Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        if (a[3] && !virtual_memory.Access(a[2] + a[3] - 1, 1, true))
            return 0;
        return 0;
    }
    // CL 6Eh, over : the palette signature, the normal
    // gate, then the default palette answers FALSE and every other TRUE while
    // ICM is off. An EMF DC records nothing with ICM off.
    API_CASE("ColorCorrectPalette")
    {
        if (Gdi::GetObjectType(a[1]) != 5 || !Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        return a[1] == Gdi::DefaultPalette() ? 0 : 1;
    }
    // CL C1h, over : the exact gate, whose error exit
    // answers -1. The display driver of this port has no DrawEscape entry
    // (DC+34h +84h), so a valid DC answers zero, QUERYESCSUPPORT with fewer
    // than four bytes included.
    API_CASE("DrawEscape")
    {
        if (!Gdi::ValidDC(a[0]) || GdiDcKind(a[0]) == 1)
        {
            last_error = 6;
            return ~0u;
        }
        if ((a[2] >> 16) && !a[3])
            return ~0u;
        return 0;
    }
    // the LS adapter, GDI.495 : a null pointer or
    // an empty comment is 87; the exact gate refuses a WMF DC, and the adapter
    // turns that into 87 as well. An EMF DC records EMR_GDICOMMENT, except a
    // "GDIC" comment of a public type (bit 31), which GDI keeps to itself; a
    // multi-format comment (bit 30) adds its RECTL to the bounds. Every other
    // DC answers TRUE with nothing done.
    API_CASE("GdiComment")
    {
        const unsigned dc = a[0], bytes = a[1], data = a[2];
        if (!data || !bytes)
        {
            last_error = 87;
            return 0;
        }
        if (!virtual_memory.Access(data, bytes, false))
        {
            last_error = 87;
            return 0;
        }
        const unsigned kind = GdiDcKind(dc);
        if (kind == 1 || !Gdi::ValidDC(dc))
        {
            last_error = 87;
            return 0;
        }
        if (kind != 2)
            return 1;
        unsigned head[6] = {};
        Read(data, head, bytes >= 24 ? 24 : bytes & ~3u);
        const bool public_comment = bytes >= 8 && head[0] == 0x43494447 && (head[1] & 0x80000000u);
        if (public_comment)
            return 1;
        const unsigned count = bytes & 0xffff;
        auto *payload = new unsigned char[4 + count];
        payload[0] = (unsigned char)count;
        payload[1] = (unsigned char)(count >> 8);
        payload[2] = payload[3] = 0;
        Read(data, payload + 4, count);
        bool ok = EnhancedRecordRaw(dc, EmrGdiComment, payload, 4 + count);
        delete[] payload;
        if (ok && bytes >= 24 && head[0] == 0x43494447 && (head[1] & 0x40000000u))
            EnhancedBoundsRaw(dc, int(short(head[2])), int(short(head[3])), int(short(head[4])),
                              int(short(head[5])));
        if (!ok)
            last_error = 87;
        return ok ? 1 : 0;
    }
    // and over GDI.194 and GDI.193. The
    // thunk table points straight at the exports: there is no DC check. The
    // RECT goes over as four words; GetBoundsRect widens four words back
    // into the caller's RECT on every path.
    API_CASE("SetBoundsRect")
    {
        Gdi::RECT rect{}, *pointer = nullptr;
        if (a[1])
        {
            int wide[4] = {};
            if (!Read(a[1], wide, sizeof wide))
                return 0;
            rect = {short(wide[0]), short(wide[1]), short(wide[2]), short(wide[3])};
            pointer = &rect;
        }
        return Gdi::SetBoundsRect(a[0], pointer, a[2]) & 0xffff;
    }
    API_CASE("GetBoundsRect")
    {
        Gdi::RECT rect{};
        const unsigned result = Gdi::GetBoundsRect(a[0], a[1] ? &rect : nullptr, a[2]);
        if (a[1] && result == 3)
        {
            const int wide[4] = {short(rect.left), short(rect.top), short(rect.right), short(rect.bottom)};
            Store(a[1], wide, sizeof wide);
        }
        return result;
    }
    // CL 80h, over : the file is read and the header
    // checked for type 1, the signature " EMF", a nonzero handle count and a
    // byte count that is a multiple of four. nSize, nVersion and nBytes are
    // not held against the file.
    API_CASE("GetEnhMetaFileA")
        return EnhancedOpenFile(a[0]);
    // The miter
    // limit is the float at DC+74h. SetMiterLimit records EMR_SETMITERLIMIT
    // before it looks at the value, refuses anything below 1.0 with FALSE and
    // no LastError, and hands the old value out first.
    API_CASE("GetMiterLimit")
    {
        unsigned value = 0;
        if (!Gdi::MiterLimit(a[0], nullptr, value))
        {
            last_error = 6;
            return 0;
        }
        return Store(a[1], &value, 4) ? 1 : 0;
    }
    API_CASE("SetMiterLimit")
    {
        if (a[2] && !virtual_memory.Access(a[2], 4, true))
            return 0;
        const unsigned kind = GdiDcKind(a[0]);
        if (kind == 1 || !Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        if (kind == 2)
        {
            unsigned char payload[4];
            memcpy(payload, &a[1], 4);
            if (!EnhancedRecordRaw(a[0], EmrSetMiterLimit, payload, 4))
                return 0;
        }
        // Windows 98 compares in software arithmetic: a negative value and
        // anything below 1.0 fail, a denormal counts as zero, NaN and
        // infinity pass.
        const unsigned value = a[1];
        const bool negative = (value & 0x80000000u) != 0;
        const unsigned exponent = (value >> 23) & 0xff;
        const bool nan_or_infinity = exponent == 0xff;
        if (!nan_or_infinity && (negative || exponent == 0 || (value & 0x7fffffffu) < FloatOne()))
            return 0;
        unsigned previous = 0;
        Gdi::MiterLimit(a[0], &value, previous);
        if (a[2])
            Store(a[2], &previous, 4);
        return 1;
    }
    // the LS adapter, GDI.100. The coordinates go
    // over as words. A null procedure fails the code check of GDI.100 and
    // draws nothing. The digital differential analyser in 16-bit arithmetic
    // leaves the end point out; the procedure gets (x, y, lParam).
    API_CASE("LineDDA")
    {
        const int x1 = short(a[0]), y1 = short(a[1]), x2 = short(a[2]), y2 = short(a[3]);
        const unsigned procedure = a[4], parameter = a[5];
        if (!procedure || !virtual_memory.Access(procedure, 1, false))
            return 1;
        int dx = x2 - x1, dy = y2 - y1;
        const int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
        const int adx = short(dx < 0 ? -dx : dx), ady = short(dy < 0 ? -dy : dy);
        const bool y_major = ady >= adx;
        const int major = y_major ? ady : adx, minor = y_major ? adx : ady;
        int e = y2 >= y1 ? major >> 1 : (major + 1) >> 1;
        int x = x1, y = y1;
        for (int i = 0; i < major && state != Failed && state != Exited; ++i)
        {
            const unsigned call[3] = {unsigned(x), unsigned(y), parameter};
            Call(procedure, call, 3);
            if (y_major) y = short(y + sy); else x = short(x + sx);
            e = short(e - minor);
            const bool step = y2 >= y1 ? e < 0 : e <= 0;
            if (step)
            {
                e = short(e + major);
                if (y_major) x = short(x + sx); else y = short(y + sy);
            }
        }
        return 1;
    }
    // CL 0, over : the normal gate, then a device
    // without RC_PALETTE leaves at once with a nonzero register. The display
    // of this port has 32 bits per pixel.
    API_CASE("SetMagicColors")
    {
        if (!Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        if (Gdi::DeviceCaps(a[0], 38) & 0x100)
        {
            Fail("SetMagicColors on a palette device is not ported: GDI rewrites the system "
                 "palette entries 8, 9 and 246");
            return 0;
        }
        return 1;
    }
    // CL 86h, over : the exact gate, EMR_SETMETARGN on
    // an EMF DC, then the application clip moves into the meta region.
    API_CASE("SetMetaRgn")
    {
        const unsigned kind = GdiDcKind(a[0]);
        if (kind == 1 || !Gdi::ValidDC(a[0]))
        {
            last_error = 6;
            return 0;
        }
        if (kind == 2 && !EnhancedRecordRaw(a[0], EmrSetMetaRgn, nullptr, 0))
            return 0;
        return Gdi::SetMetaRgn(a[0]);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}

// The rectangle a drawing call adds to the bounds of its DC (GDI and
// its callers: the blit family, lines, the shapes of segment
// 10, pixels, polygons, flood fills, text). Runs only
// while the DC accumulates.
void Application::GdiAccumulate(ApiName name, const unsigned *a)
{
    const unsigned dc = a[0];
    if (!Gdi::AccumulatesBounds(dc))
        return;
    const auto box = [&](int l, int t, int r, int b) { Gdi::AccumulateLogical(dc, l, t, r, b); };
    const auto points = [&](unsigned at, unsigned count) {
        int left = 0x7fffffff, top = 0x7fffffff, right = -0x7fffffff, bottom = -0x7fffffff;
        for (unsigned i = 0; i < count && i < 65536; ++i)
        {
            int p[2];
            if (!Read(at + i * 8, p, 8)) return;
            if (p[0] < left) left = p[0];
            if (p[1] < top) top = p[1];
            if (p[0] + 1 > right) right = p[0] + 1;
            if (p[1] + 1 > bottom) bottom = p[1] + 1;
        }
        if (count) box(left, top, right, bottom);
    };
    switch (name.id)
    {
    API_CASE("Rectangle") API_CASE("Ellipse") API_CASE("RoundRect") API_CASE("Arc") API_CASE("Chord")
    API_CASE("Pie") API_CASE("ArcTo")
        box(int(a[1]), int(a[2]), int(a[3]), int(a[4]));
        break;
    API_CASE("PatBlt") API_CASE("BitBlt") API_CASE("StretchBlt") API_CASE("StretchDIBits")
    API_CASE("SetDIBitsToDevice") API_CASE("MaskBlt")
        box(int(a[1]), int(a[2]), int(a[1] + a[3]), int(a[2] + a[4]));
        break;
    API_CASE("SetPixel") API_CASE("SetPixelV")
        box(int(a[1]), int(a[2]), int(a[1]) + 1, int(a[2]) + 1);
        break;
    API_CASE("LineTo")
    {
        Gdi::POINT from{};
        if (!Gdi::CurrentPosition(dc, from)) break;
        const int x = int(a[1]), y = int(a[2]);
        box(from.x < x ? from.x : x, from.y < y ? from.y : y, (from.x > x ? from.x : x) + 1,
            (from.y > y ? from.y : y) + 1);
        break;
    }
    API_CASE("Polyline") API_CASE("Polygon") API_CASE("PolyBezier") API_CASE("PolylineTo")
    API_CASE("PolyBezierTo")
        points(a[1], a[2]);
        break;
    API_CASE("PolyPolygon") API_CASE("PolyPolyline")
    {
        unsigned total = 0;
        for (unsigned i = 0; i < a[3] && i < 4096; ++i)
            total += Read32(a[2] + i * 4);
        points(a[1], total);
        break;
    }
    API_CASE("FillRect") API_CASE("FrameRect") API_CASE("InvertRect")
    {
        int r[4];
        if (Read(a[1], r, sizeof r)) box(r[0], r[1], r[2], r[3]);
        break;
    }
    API_CASE("TextOutA") API_CASE("ExtTextOutA")
    {
        const unsigned text = name.id == API_ID("TextOutA") ? a[3] : a[5];
        const unsigned count = name.id == API_ID("TextOutA") ? a[4] : a[6];
        const unsigned scratch = Allocate(8);
        if (!scratch) break;
        const unsigned query[4] = {dc, text, count, scratch};
        if (GdiApi("GetTextExtentPoint32A", query))
            box(int(a[1]), int(a[2]), int(a[1]) + int(Read32(scratch)), int(a[2]) + int(Read32(scratch + 4)));
        Free(scratch);
        break;
    }
    API_CASE("FloodFill") API_CASE("ExtFloodFill")
    {
        Gdi::RECT clip{};
        if (Gdi::GetClipBox(dc, &clip)) box(clip.left, clip.top, clip.right, clip.bottom);
        break;
    }
    default:
        break;
    }
}
} // namespace X86
