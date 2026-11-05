// SPDX-License-Identifier: GPL-2.0-or-later
// TrueType fonts as Win98 GDI realizes them.
// The rasterizer is the original x86 driver of GDI32.DLL, run by
// src/truetype_machine.cpp. This file is the part GDI.EXE plays around it:
// the directory entry of a font file, the height to ppem
// search and the realized metrics,
// the transformation, the width table and the
// glyph and outline queries of segment 13. A realized font carries a
// synthesized FNT 3.0 header with its advance widths, so the measuring code
// of the DIB engine port (src/dib_text.cpp) keeps working unchanged; the
// glyphs themselves come from the driver on demand and are drawn at their
// own origin, overhanging the advance cell where the outline does.
#pragma once
#include "dib_text.h"
namespace Gdi { struct TEXTMETRICA; }
namespace GdiTrueType
{
using U = unsigned;
// Starts the driver machine with the guest's file and registry readers. The
// reader returns malloc'ed bytes of a guest path; the query answers one
// registry value (see TrueTypeMachine::Query).
using Reader = bool (*)(const char *path, unsigned char *&data, U &size);
using Query = bool (*)(const char *key, const char *name, unsigned char *data, U &size);
bool Start(Reader, Query);
bool Available();
const char *Failure();

// One installed.TTF: GDI's directory entry, derived from the 9Ah-byte header
// of driver entry 31 the way derives it (values in font units).
struct Face
{
    char leaf[16] = {};         // file name as the registry names it
    char family[32] = {};       // name ID 1
    char style[32] = {};        // name ID 2
    char full[64] = {};         // name ID 4
    U file = 0;                 // driver file object (entry 34)
    unsigned char header[0x9a] = {};
    int upem = 0, ascent = 0, cell = 0, internal = 0, external = 0, average = 0, maximum = 0;
    U weight = 0, charset = 0, pitch = 0, type = 0;
    bool italic = false, underline = false, strikeout = false;
    U first = 0, last = 0;      // dfFirstChar, dfLastChar of the entry
    U symbol_base = 0;          // F000h for a symbol cmap, else 0
    U glyphs = 0;               // maxp.numGlyphs
};
// Opens C:\WINDOWS\FONTS\<leaf> (or a qualified path) through the driver.
const Face *Install(const char *file);
U FaceCount();
const Face *FaceAt(U);

// The realized font. `fnt` is a FNT 3.0 header (dfReserved1 "TT", line rows at
// 84h..8Dh, simulated overhang at 8Eh) with a 256-entry width table; its glyph
// offsets all point at one blank cell and are never drawn.
struct Glyph;
struct Font
{
    const Face *face = nullptr;
    int height = 0, width = 0;  // the request
    int escapement = 0;        // supported quarter-turn text direction
    int ppem = 0;
    U xscale = 0x100;           // lfWidth factor in 1/256
    bool italic = false;        // simulated by shear
    bool bold = false;          // simulated
    bool underline = false, strikeout = false;
    DibText::Font view;         // header and widths for the DIB engine port
    unsigned char *fnt = nullptr;
    U fnt_length = 0;
    int ascent = 0, descent = 0, internal = 0, external = 0, average = 0, maximum = 0;
    int underline_top = 0, underline_thickness = 0, strike_top = 0, strike_thickness = 0;
    U weight = 0;
    unsigned short index[256] = {};   // glyph index of each ANSI code
    Glyph *glyphs[256] = {};          // text output cache by code
    Glyph **by_index = nullptr;       // cache of IndexGlyph
};
// The logical request: LOGFONT height, width, weight, italic, underline and
// strikeout. The same request answers the same Font object. `describe` builds
// only the metrics (every advance the average width, no glyph table) for the
// font catalogue, which describes each file without drawing with it; such an
// object is not shared.
const Font *Realize(const Face *, int height, int width, U weight, bool italic, bool underline, bool strikeout,
                    bool describe = false, int escapement = 0);
inline const Font *FontOf(const DibText::Font &view) { return static_cast<const Font *>(view.outline); }
// Transform integer boundaries (not pixel centers) about the text origin.
inline void Rotate(int angle, int &x, int &y)
{
    const int old_x=x, old_y=y;
    switch ((angle / 900 % 4 + 4) % 4) {
    case 1: x=old_y; y=-old_x; break;
    case 2: x=-old_x; y=-old_y; break;
    case 3: x=-old_y; y=old_x; break;
    }
}
inline Win98::Rect RotateRect(int angle, const Win98::Rect &r)
{
    int x1=r.left,y1=r.top,x2=r.right,y2=r.bottom;
    Rotate(angle,x1,y1);Rotate(angle,x2,y2);
    return {x1<x2?x1:x2,y1<y2?y1:y2,x1>x2?x1:x2,y1>y2?y1:y2};
}

// The glyph of one ANSI code for text output: 1 bpp rows padded to DWORDs,
// the black box, its origin relative to the pen on the baseline (y up) and
// the advance. False when the driver has no answer.
struct GlyphImage
{
    const unsigned char *bits = nullptr;
    int width = 0, height = 0, stride = 0, origin_x = 0, origin_y = 0, advance = 0;
};
bool TextGlyph(const Font &, unsigned char code, GlyphImage &);
// The glyph index of a Unicode character (0 when the font has none) and the
// image of a glyph by index, for a caller with its own code page (the text
// mode of the MS-DOS window, src/dos_vm_truetype.cpp).
U UnicodeIndex(const Font &, U unicode);
bool IndexGlyph(const Font &, U index, GlyphImage &);

// Printer driver uses 24-byte metrics with fixed-point
// origins/increments, and passes its flag mapping straight to entry 24.
U EngineContext(const Font &, int angle, int dpi_x, int dpi_y, U spot_x, U spot_y);
U EngineGlyph(const Font &, U character, bool index, U format, unsigned char *buffer, U size, int (&metrics)[6]);

// GetGlyphOutline, GDI: `format` & 7 is the GGO format (3,
// GGO_BEZIER, fails as in Win98), bit 80h GGO_GLYPH_INDEX. `matrix` is a MAT2
// in 16.16 or null. The result is the driver's size or ~0u; the glyph metrics
// come back in Win32 GLYPHMETRICS order (black box, origin, cell increments).
struct GlyphMetrics
{
    U black_x = 0, black_y = 0;
    int origin_x = 0, origin_y = 0;
    short cell_x = 0, cell_y = 0;
};
U GlyphOutline(const Font &, U character, U format, const int *matrix, unsigned char *buffer, U size,
               GlyphMetrics &);
// GetCharABCWidths, : A, B, C per code from the glyph metrics.
bool AbcWidths(const Font &, U first, U last, int *abc);
// GetFontData, through entry 32. ~0u on failure.
U FontData(const Font &, U table, U offset, unsigned char *buffer, U size);
// GetKerningPairs, through entry 25 in mode 3: Win16 records of
// first, second (ANSI codes) and the amount in pixels, six bytes each. Answers
// the number written, or the total when `pairs` is null.
U KerningPairs(const Font &, U count, unsigned char *pairs);
// GetOutlineTextMetricsA: together with GDI32's widening at
// GDI. `tm` is the TEXTMETRIC of the font. With a null buffer
// the answer is the size; otherwise `size` bytes of the Win32 structure and
// its names are written and the answer is 1 (0 on failure).
U OutlineTextMetrics(const Font &, const Gdi::TEXTMETRICA &tm, unsigned char *buffer, U size);

// Text output of one run in the manner of DIB_EXTTEXTOUT, with every glyph at
// its own origin. `extent` is the measured run of DibText::ExtTextOut.
bool Draw(DibEng::Bitmap &, int x, int y, const Win98::Rect &clip, const unsigned char *text, int count,
          const Font &, DibText::DrawMode &, const short *dx, const Win98::Rect *opaque, U options);
} // namespace GdiTrueType
