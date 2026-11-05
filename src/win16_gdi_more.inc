// SPDX-License-Identifier: GPL-2.0-or-later
// The GDI.EXE ordinals no program of the matrix had asked for yet, added in
// one pass over the export table of GDI.EXE (tools/win16_ordinal_inventory.py). Included by src/win16_user.cpp.
// Each gate unpacks the Pascal frame and hands the call to the 32-bit entry
// of this port; POINT, SIZE and RECT are two and four words on this side and
// two and four dwords there, so arrays and out parameters are widened into a
// block of their own and narrowed back. The old forms that answer a pair in
// DX:AX (GetCurrentPosition, ScaleWindowExt and the others) go through the
// Ex entries, as SetWindowOrg and its neighbours already do. The word counts
// are in src/win98_win16_user.h.
bool Application::Win16GdiMore(unsigned ordinal, const Win16Frame &frame, unsigned words, unsigned &result,
                               bool &handled)
{
    handled = true;
    auto arg = [&](unsigned position) { return frame.Word(words - 1 - position); };
    auto number = [&](unsigned position) { return (unsigned)(int)(short)arg(position); };
    auto pointer = [&](unsigned position) { return frame.Pointer(words - 2 - position); };
    auto dword = [&](unsigned position) { return frame.Long(words - 2 - position); };
    // The pair the native entry wrote at `at`, as DX:AX.
    auto pair = [&](unsigned at) { return ((Read32(at + 4) & 0xffff) << 16) | (Read32(at) & 0xffff); };
    // A POINT or SIZE written back as two words.
    auto pair_out = [&](unsigned from, unsigned to) {
        if (!to)
            return;
        Win16Word(to, Read32(from));
        Win16Word(to + 2, Read32(from + 4));
    };
    auto rect_in = [&](unsigned from, unsigned to) -> unsigned {
        if (!from)
            return 0;
        Gdi::RECT r{};
        Win16ReadRect(from, r);
        Store(to, &r, sizeof r);
        return to;
    };
    auto rect_out = [&](unsigned from, unsigned to) {
        if (!to)
            return;
        const Gdi::RECT r{(int)Read32(from), (int)Read32(from + 4), (int)Read32(from + 8), (int)Read32(from + 12)};
        Win16WriteRect(to, r);
    };
    // An array of 16-bit POINTs widened into a block of its own, which the
    // caller frees; a polygon can be larger than the staging block.
    auto points_in = [&](unsigned from, unsigned count) -> unsigned {
        if (!from || !count || count > 0x4000)
            return 0;
        const unsigned block = Allocate(count * 8);
        for (unsigned i = 0; block && i < count; ++i)
        {
            Write32(block + i * 8, (unsigned)(int)(short)Win16Word(from + i * 4));
            Write32(block + i * 8 + 4, (unsigned)(int)(short)Win16Word(from + i * 4 + 2));
        }
        return block;
    };
    switch (ordinal)
    {
    // ---- plain words
    case 6: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetPolyFillMode", a); return true; }
    case 8: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetTextCharacterExtra", a); return true; }
    case 10: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("SetTextJustification", a); return true; }
    case 23: // Arc(hdc, 8x int), GDI 15 like Pie and Chord
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4), number(5), number(6), number(7), number(8)};
        result = Api("Arc", a);
        return true;
    }
    case 28: // RoundRect(hdc, left, top, right, bottom, width, height)
    {
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4), number(5), number(6)};
        result = Api("RoundRect", a);
        return true;
    }
    case 32: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("OffsetClipRgn", a); return true; }
    case 40: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("FillRgn", a); return true; }
    case 41: { const unsigned a[] = {arg(0), arg(1), arg(2), number(3), number(4)}; result = Api("FrameRgn", a); return true; }
    case 42: { const unsigned a[] = {arg(0), arg(1)}; result = Api("InvertRgn", a); return true; }
    case 43: { const unsigned a[] = {arg(0), arg(1)}; result = Api("PaintRgn", a); return true; }
    case 54: { const unsigned a[] = {number(0), number(1), number(2), number(3)}; result = Api("CreateEllipticRgn", a); return true; }
    case 72: { const unsigned a[] = {arg(0), arg(1)}; result = Api("EqualRgn", a); return true; }
    case 84: { const unsigned a[] = {arg(0)}; result = Api("GetPolyFillMode", a); return true; }
    case 88: { const unsigned a[] = {arg(0)}; result = Api("GetStretchBltMode", a); return true; }
    case 89: { const unsigned a[] = {arg(0)}; result = Api("GetTextCharacterExtra", a); return true; }
    case 101: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("OffsetRgn", a); return true; }
    case 103: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("PtVisible", a); return true; }
    case 156: { const unsigned a[] = {arg(0), number(1), number(2)}; result = Api("CreateDiscardableBitmap", a); return true; }
    case 345: { const unsigned a[] = {arg(0)}; result = Api("GetTextAlign", a); return true; }
    case 349: { const unsigned a[] = {arg(0), dword(1)}; result = Api("SetMapperFlags", a); return true; }
    case 372: { const unsigned a[] = {arg(0), number(1), number(2), dword(3), arg(5)}; result = Api("ExtFloodFill", a); return true; }
    case 376: { const unsigned a[] = {arg(0), pointer(1)}; result = Api("ResetDCA", a); return true; }
    case 444:
    {
        const unsigned a[] = {number(0), number(1), number(2), number(3), number(4), number(5)};
        result = Api("CreateRoundRectRgn", a);
        return true;
    }
    case 508: { const unsigned a[] = {arg(0), arg(1), number(2)}; result = Api("ExtSelectClipRgn", a); return true; }
    case 524: { const unsigned a[] = {arg(0)}; result = Api("GetArcDirection", a); return true; }
    case 525: { const unsigned a[] = {arg(0), number(1)}; result = Api("SetArcDirection", a); return true; }
    case 529: { const unsigned a[] = {arg(0)}; result = Api("CreateHalftonePalette", a); return true; }
    case 606: { const unsigned a[] = {arg(0), dword(1), arg(3)}; result = Api("SetMagicColors", a); return true; }
    case 611: { const unsigned a[] = {arg(0), arg(1), number(2)}; result = Api("GetRandomRgn", a); return true; }
    case 612: { const unsigned a[] = {arg(0)}; result = Api("GetTextCharset", a); return true; }
    case 616: { const unsigned a[] = {arg(0)}; result = Api("GetFontLanguageInfo", a); return true; }
    // The paths: one DC, SelectClipPath with its mode.
    case 511: case 512: case 513: case 514: case 515: case 516: case 518: case 520: case 521: case 522:
    {
        const unsigned a[] = {arg(0)};
        result = Api(ordinal == 511   ? "AbortPath"
                     : ordinal == 512 ? "BeginPath"
                     : ordinal == 513 ? "CloseFigure"
                     : ordinal == 514 ? "EndPath"
                     : ordinal == 515 ? "FillPath"
                     : ordinal == 516 ? "FlattenPath"
                     : ordinal == 518 ? "PathToRegion"
                     : ordinal == 520 ? "StrokeAndFillPath"
                     : ordinal == 521 ? "StrokePath"
                                      : "WidenPath",
                     a);
        return true;
    }
    case 519: { const unsigned a[] = {arg(0), number(1)}; result = Api("SelectClipPath", a); return true; }
    // GDISelectPalette and GDIRealizePalette are the bodies USER's
    // SelectPalette and RealizePalette call (USER.282, USER.283).
    case 361: { const unsigned a[] = {arg(0), arg(1), arg(2)}; result = Api("SelectPalette", a); return true; }
    case 362: { const unsigned a[] = {arg(0)}; result = Api("RealizePalette", a); return true; }
    // The colour table of a DIB section: RGBQUAD is the same on both sides.
    case 602: case 603:
    {
        const unsigned a[] = {arg(0), arg(1), arg(2), pointer(3)};
        result = Api(ordinal == 602 ? "SetDIBColorTable" : "GetDIBColorTable", a);
        return true;
    }
    case 136: // RemoveFontResource(file)
    {
        const unsigned a[] = {pointer(0)};
        result = a[0] ? Api("RemoveFontResourceA", a) : 0u;
        return true;
    }
    // RASTERIZER_STATUS is three shorts on both sides.
    case 313: { const unsigned a[] = {pointer(0), number(2)}; result = Api("GetRasterizerCaps", a); return true; }
    case 311: // GetFontData(hdc, table, offset, buffer, bytes)
    {
        const unsigned a[] = {arg(0), dword(1), dword(3), pointer(5), dword(7)};
        result = Api("GetFontData", a);
        return true;
    }
    case 526: // GetMiterLimit(hdc, limit): a FLOAT on both sides.
    {
        const unsigned a[] = {arg(0), pointer(1)};
        result = Api("GetMiterLimit", a);
        return true;
    }
    case 527: // SetMiterLimit(hdc, limit, old): the FLOAT travels as its bits.
    {
        const unsigned a[] = {arg(0), dword(1), pointer(3)};
        result = Api("SetMiterLimit", a);
        return true;
    }

    // ---- the pairs in DX:AX and the Ex forms
    case 16: // ScaleWindowExt(hdc, xnum, xdenom, ynum, ydenom)
    case 18: // ScaleViewportExt: both answer the previous extents.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4), scratch};
        result = Api(ordinal == 16 ? "ScaleWindowExtEx" : "ScaleViewportExtEx", a) ? pair(scratch) : 0u;
        return true;
    }
    case 78:  // GetCurrentPosition(hdc)
    case 149: // GetBrushOrg(hdc)
    case 162: // GetBitmapDimension(bitmap)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api(ordinal == 78 ? "GetCurrentPositionEx" : ordinal == 149 ? "GetBrushOrgEx" : "GetBitmapDimensionEx", a)
                     ? pair(scratch)
                     : 0u;
        return true;
    }
    case 163: // SetBitmapDimension(bitmap, x, y): the previous dimension.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), number(1), number(2), scratch};
        result = Api("SetBitmapDimensionEx", a) ? pair(scratch) : 0u;
        return true;
    }
    case 468: case 469: case 470: case 472: case 473: case 474: case 475: case 486:
    {
        // GetBitmapDimensionEx, GetBrushOrgEx, GetCurrentPositionEx, the four
        // queries of the mapping and GetAspectRatioFilterEx: a handle and the
        // POINT or SIZE to fill.
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api(ordinal == 468   ? "GetBitmapDimensionEx"
                     : ordinal == 469 ? "GetBrushOrgEx"
                     : ordinal == 470 ? "GetCurrentPositionEx"
                     : ordinal == 472 ? "GetViewportExtEx"
                     : ordinal == 473 ? "GetViewportOrgEx"
                     : ordinal == 474 ? "GetWindowExtEx"
                     : ordinal == 475 ? "GetWindowOrgEx"
                                      : "GetAspectRatioFilterEx",
                     a);
        if (result)
            pair_out(scratch, pointer(1));
        return true;
    }
    case 476: case 477: case 478: case 479: case 480: case 481: case 482: case 483:
    {
        // OffsetViewportOrgEx, OffsetWindowOrgEx, SetBitmapDimensionEx, the
        // four setters of the mapping and MoveToEx: a handle, two numbers and
        // the previous pair.
        const unsigned scratch = Win16Scratch(), out = pointer(3);
        if (!scratch) return false;
        const unsigned a[] = {arg(0), number(1), number(2), out ? scratch : 0u};
        result = Api(ordinal == 476   ? "OffsetViewportOrgEx"
                     : ordinal == 477 ? "OffsetWindowOrgEx"
                     : ordinal == 478 ? "SetBitmapDimensionEx"
                     : ordinal == 479 ? "SetViewportExtEx"
                     : ordinal == 480 ? "SetViewportOrgEx"
                     : ordinal == 481 ? "SetWindowExtEx"
                     : ordinal == 482 ? "SetWindowOrgEx"
                                      : "MoveToEx",
                     a);
        if (result && out)
            pair_out(scratch, out);
        return true;
    }
    case 484: // ScaleViewportExtEx(hdc, xnum, xdenom, ynum, ydenom, old)
    case 485: // ScaleWindowExtEx
    {
        const unsigned scratch = Win16Scratch(), out = pointer(5);
        if (!scratch) return false;
        const unsigned a[] = {arg(0), number(1), number(2), number(3), number(4), out ? scratch : 0u};
        result = Api(ordinal == 484 ? "ScaleViewportExtEx" : "ScaleWindowExtEx", a);
        if (result && out)
            pair_out(scratch, out);
        return true;
    }
    case 471: // GetTextExtentPoint(hdc, text, count, size)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), pointer(1), number(3), scratch};
        result = Api("GetTextExtentPointA", a);
        if (result)
            pair_out(scratch, pointer(4));
        return true;
    }

    // ---- rectangles
    case 55: // CreateEllipticRgnIndirect(rect)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {rect_in(pointer(0), scratch)};
        result = a[0] ? Api("CreateEllipticRgnIndirect", a) : 0u;
        return true;
    }
    case 134: // GetRgnBox(region, rect)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), scratch};
        result = Api("GetRgnBox", a);
        if (result)
            rect_out(scratch, pointer(1));
        return true;
    }
    case 181: // RectInRegion(region, rect)
    case 466: // RectInRegion with the edges of Win32, the same entry here
    case 465: // RectVisible with the edges of Win32
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned a[] = {arg(0), rect_in(pointer(1), scratch)};
        result = a[1] ? Api(ordinal == 465 ? "RectVisible" : "RectInRegion", a) : 0u;
        return true;
    }
    case 193: // SetBoundsRect(hdc, rect, flags)
    case 194: // GetBoundsRect(hdc, rect, flags)
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch) return false;
        const unsigned rect = ordinal == 193 ? rect_in(pointer(1), scratch) : (pointer(1) ? scratch : 0u);
        const unsigned a[] = {arg(0), rect, arg(3)};
        result = Api(ordinal == 193 ? "SetBoundsRect" : "GetBoundsRect", a);
        if (ordinal == 194 && rect)
            rect_out(scratch, pointer(1));
        return true;
    }

    // ---- point arrays
    case 36:  // Polygon(hdc, points, count)
    case 37:  // Polyline(hdc, points, count)
    case 502: // PolyBezier(hdc, points, count)
    case 503: // PolyBezierTo(hdc, points, count)
    {
        const int count = (short)arg(3);
        const unsigned block = count > 0 ? points_in(pointer(1), (unsigned)count) : 0u;
        const unsigned a[] = {arg(0), block, block ? (unsigned)count : 0u};
        result = block ? Api(ordinal == 36    ? "Polygon"
                             : ordinal == 37  ? "Polyline"
                             : ordinal == 502 ? "PolyBezier"
                                              : "PolyBezierTo",
                             a)
                       : 0u;
        if (block) Free(block);
        return true;
    }
    case 63: // CreatePolygonRgn(points, count, mode)
    {
        const int count = (short)arg(2);
        const unsigned block = count > 0 ? points_in(pointer(0), (unsigned)count) : 0u;
        const unsigned a[] = {block, block ? (unsigned)count : 0u, number(3)};
        result = block ? Api("CreatePolygonRgn", a) : 0u;
        if (block) Free(block);
        return true;
    }
    case 450: // PolyPolygon(hdc, points, counts, polygons)
    case 451: // CreatePolyPolygonRgn(points, counts, polygons, mode)
    {
        const unsigned first = ordinal == 450 ? 1 : 0;
        const unsigned points = pointer(first), counts = pointer(first + 2);
        const int polygons = (short)arg(first + 4);
        result = 0;
        if (!points || !counts || polygons <= 0 || polygons > 0x1000)
            return true;
        const unsigned widths = Allocate(polygons * 4);
        unsigned total = 0;
        for (int i = 0; widths && i < polygons; ++i)
        {
            const int n = (short)Win16Word(counts + i * 2);
            Write32(widths + i * 4, (unsigned)n);
            total += n > 0 ? (unsigned)n : 0u;
        }
        const unsigned block = widths ? points_in(points, total) : 0u;
        if (block && ordinal == 450)
        {
            const unsigned a[] = {arg(0), block, widths, (unsigned)polygons};
            result = Api("PolyPolygon", a);
        }
        else if (block)
        {
            const unsigned a[] = {block, widths, (unsigned)polygons, number(5)};
            result = Api("CreatePolyPolygonRgn", a);
        }
        if (block) Free(block);
        if (widths) Free(widths);
        return true;
    }

    // ---- structures
    case 49: // CreateBitmapIndirect(bitmap): BITMAP of the 16-bit side is four
             // ints, two bytes and a far pointer; the native one four longs,
             // two words and a flat pointer.
    {
        const unsigned at = pointer(0), scratch = Win16Scratch();
        if (!scratch) return false;
        if (!at) { result = 0; return true; }
        for (unsigned i = 0; i < 4; ++i)
            Write32(scratch + i * 4, (unsigned)(int)(short)Win16Word(at + i * 2));
        unsigned char counts[2] = {};
        Read(at + 8, counts, 2);
        Win16Word(scratch + 16, counts[0]);
        Win16Word(scratch + 18, counts[1]);
        const unsigned offset = Win16Word(at + 10), selector = Win16Word(at + 12);
        Write32(scratch + 20, selector ? Win16Linear(selector, offset) : 0u);
        const unsigned a[] = {scratch};
        result = Api("CreateBitmapIndirect", a);
        return true;
    }
    case 523: // ExtCreatePen(style, width, brush, count, styles): LOGBRUSH of
              // the 16-bit side is a word, a COLORREF and a short.
    {
        const unsigned scratch = Win16Scratch(), brush = pointer(4);
        if (!scratch) return false;
        if (brush)
        {
            Write32(scratch, Win16Word(brush));
            Write32(scratch + 4, Win16Word(brush + 2) | Win16Word(brush + 4) << 16);
            Write32(scratch + 8, (unsigned)(int)(short)Win16Word(brush + 6));
        }
        const unsigned a[] = {dword(0), dword(2), brush ? scratch : 0u, dword(6), pointer(8)};
        result = Api("ExtCreatePen", a);
        return true;
    }
    case 307: // GetCharABCWidths(hdc, first, last, widths): ABC is a short,
              // a word and a short here, three ints there.
    case 350: // GetCharWidth(hdc, first, last, widths): ints, a word each here.
    {
        const unsigned scratch = Win16Scratch(), out = pointer(3);
        if (!scratch) return false;
        const unsigned first = arg(1), last = arg(2);
        const unsigned size = ordinal == 307 ? 12 : 4;
        if (!out || last < first || (last - first + 1) * size > 4096) { result = 0; return true; }
        const unsigned a[] = {arg(0), first, last, scratch};
        result = Api(ordinal == 307 ? "GetCharABCWidthsA" : "GetCharWidthA", a);
        for (unsigned i = 0; result && i <= last - first; ++i)
        {
            if (ordinal == 350)
            {
                Win16Word(out + i * 2, Read32(scratch + i * 4));
                continue;
            }
            for (unsigned k = 0; k < 3; ++k)
                Win16Word(out + i * 6 + k * 2, Read32(scratch + i * 12 + k * 4));
        }
        return true;
    }
    case 332: // GetKerningPairs(hdc, count, pairs): KERNINGPAIR is two words and
              // a short here, two words and an int there.
    {
        const unsigned out = pointer(2);
        const int count = (short)arg(1);
        if (!out || count <= 0)
        {
            const unsigned a[] = {arg(0), 0, 0};
            result = Api("GetKerningPairsA", a) & 0xffff;
            return true;
        }
        const unsigned block = Allocate((unsigned)count * 8);
        if (!block) { result = 0; return true; }
        const unsigned a[] = {arg(0), (unsigned)count, block};
        result = Api("GetKerningPairsA", a);
        for (unsigned i = 0; i < result && i < (unsigned)count; ++i)
        {
            const unsigned letters = Read32(block + i * 8);
            Win16Word(out + i * 6, letters & 0xffff);
            Win16Word(out + i * 6 + 2, letters >> 16);
            Win16Word(out + i * 6 + 4, Read32(block + i * 8 + 4));
        }
        Free(block);
        return true;
    }
    case 309: // GetGlyphOutline(hdc, char, format, metrics, bytes, buffer, matrix)
    {
        // GLYPHMETRICS is two words, a POINT and two shorts here; there the
        // two words and the POINT are dwords and the shorts stay shorts.
        // MAT2 and the outline in the buffer are the same on both sides.
        const unsigned scratch = Win16Scratch(), metrics = pointer(3);
        if (!scratch) return false;
        const unsigned a[] = {arg(0), arg(1), arg(2), scratch, dword(5), pointer(7), pointer(9)};
        result = Api("GetGlyphOutlineA", a);
        if (result != ~0u && metrics)
        {
            for (unsigned i = 0; i < 4; ++i)
                Win16Word(metrics + i * 2, Read32(scratch + i * 4));
            const unsigned increments = Read32(scratch + 16);
            Win16Word(metrics + 8, increments & 0xffff);
            Win16Word(metrics + 10, increments >> 16);
        }
        return true;
    }
    default:
        break;
    }
    handled = false;
    return false;
}
