// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "api_name.h"
#include "dibeng.h"
#include "dib_text.h"
#include "win98_rect.h"

// Native ARM adapter with a deliberately limited Win32-shaped interface.
// Public handles and DC state are native providers. DDB/BS_PATTERN bodies and
// their storage operations use the implemented GDI.EXE cores.
namespace Gdi {
using HDC = unsigned;
using HGDIOBJ = unsigned;
using HBITMAP = unsigned;
using HBRUSH = unsigned;
using HFONT = unsigned;
using COLORREF = unsigned;
using RECT = Win98::Rect;
struct POINT { int x, y; };
struct SIZE { int cx, cy; };
struct TEXTMETRICA {
    int tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading,
        tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    unsigned char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar,
        tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
};
static_assert(sizeof(TEXTMETRICA) == 56, "Win32 TEXTMETRICA wire layout");
struct BITMAPINFOHEADER {
    unsigned biSize;
    int biWidth, biHeight;
    unsigned short biPlanes, biBitCount;
    unsigned biCompression, biSizeImage;
    int biXPelsPerMeter, biYPelsPerMeter;
    unsigned biClrUsed, biClrImportant;
};
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; };
static_assert(sizeof(BITMAPINFOHEADER) == 40, "DIB header layout");
constexpr unsigned SRCCOPY = 0x00cc0020, SRCINVERT = 0x00660046,
    SRCAND = 0x008800c6, SRCPAINT = 0x00ee0086, PATCOPY = 0x00f00021,
    PATINVERT = 0x005a0049, DSTINVERT = 0x00550009, BLACKNESS = 0x42, WHITENESS = 0x00ff0062;
constexpr int TRANSPARENT = 1, OPAQUE = 2;
constexpr unsigned ETO_OPAQUE = 2, ETO_CLIPPED = 4;
constexpr COLORREF RGB(unsigned r, unsigned g, unsigned b) { return (r & 255) | ((g & 255) << 8) | ((b & 255) << 16); }

bool DrawFocusRect(HDC,const RECT*);
HDC CreateDisplayDC(HDC screen, bool information);
// The DC of a driver GDI loaded (a printer): capabilities from its GDIINFO,
// no surface; drawing goes to the driver (src/x86_print_driver.cpp).
HDC CreateDeviceDC(const unsigned char *gdiinfo, unsigned bytes, bool information);
bool DeviceInfo(HDC, const unsigned char *&gdiinfo, unsigned &bytes);
void SetDeviceEscape(HDC, void *context, int (*escape)(void *, unsigned, unsigned (&)[2]));
// The drawing half of a DC over a device driver (a printer): text, the text
// queries and pattern fills go to the driver instead of a bitmap, whoever
// calls them, GDI32 or USER (TabbedTextOut, DrawText, FillRect).
// src/x86_print_draw.cpp.
struct DeviceDraw {
    bool (*text_out)(void *context, HDC, int x, int y, unsigned options, const RECT *, const char *, unsigned count,
                     const int *advances);
    bool (*metrics)(void *context, HDC, TEXTMETRICA *);
    bool (*extent)(void *context, HDC, const char *, unsigned count, SIZE *);
    bool (*widths)(void *context, HDC, unsigned first, unsigned last, int *widths);
    bool (*pat_blt)(void *context, HDC, int x, int y, int width, int height, unsigned rop);
};
void SetDeviceDraw(HDC, const DeviceDraw *);
unsigned DeviceCaps(HDC, unsigned index);
// The size and depth of the display. A mode change moves them, and
// GetDeviceCaps is where every program reads them.
void SetDisplayCaps(int width, int height, int depth);
int DisplayDepth();
// The bits per pixel of the surface a DC draws on, 0 without one.
int SurfaceDepth(HDC);
// RealizePalette on the display writes the logical palette into the colour
// table of the screen, the way a palettized driver programs its DAC. The
// compositor installs the sink, because the published host image reads that
// table too.
void SetDisplayPaletteSink(void *, void (*)(void *, unsigned start, unsigned count,
                                            const unsigned *colors));
// The system palette of the display, in DIB order. It belongs to the device,
// not to a context: every window realizes into the same table.
void SetDisplayColors(const unsigned *colors, unsigned count);
// The SETPALETTE entry of a display driver: colours in COLORREF order go into
// the system palette. Answers how many entries were taken.
unsigned SetDisplayPalette(unsigned start, unsigned count, const unsigned *entries);
bool ValidDC(HDC);
bool DisplayLikeDC(HDC);
bool InformationDC(HDC);
bool ResizeSurfaceDC(HDC, const DibEng::Bitmap &); // Keep a leased window DC valid.
HDC CreateSurfaceDC(const DibEng::Bitmap &); // Native display adapter
// A surface with a colour table. Below nine bits per pixel a device has no
// colour of its own: the driver answers a COLORREF with the nearest entry of
// the table GDI gave it. A surface DC without a table therefore refuses every
// colour at eight bits, which is what the display needs when a DirectDraw mode
// switches the screen to 8 bits.
HDC CreateSurfaceDC(const DibEng::Bitmap &, const unsigned *colors, unsigned count, bool rgb565 = false);
bool ResizeSurfaceDC(HDC, const DibEng::Bitmap &, const unsigned *colors, unsigned count);
bool SetSurfaceColors(HDC, unsigned start, unsigned count, const unsigned *colors);
// Two device contexts that draw into the same bytes. The display DC a program
// asks GDI for shares the surface of the screen DC, so the compositor has to
// treat drawing through either of them the same way.
bool SameSurface(HDC, HDC);
// The guard of the screen's pixels. While `bits` is not null, every drawing or reading call of
// this engine on those bytes reports to the sink before it touches them. USER
// sets the bytes of the screen while a program holds the primary surface
// locked and null otherwise; the sink notes a call of another thread that
// should have waited at its gate and did not (Application::ScreenTouched).
void SetScreenGuard(const unsigned char *bits);
void SetScreenGuardSink(void (*)(bool write));
// A GPU provider supplies current bytes before any DC reads or writes them.
void SetSurfaceAccessSink(void (*)(const unsigned char *bits, bool write));
// The same report for USER's own copies of screen pixels, which do not go
// through this engine (the moves of SettleGeometry and ScrollWindowEx, the
// saved bits under a menu).
void GuardScreenBits(const unsigned char *bits, bool write);
// Every surface that still points at the old bytes gets the new ones. A mode
// change replaces the screen surface under the display DCs that are open.
unsigned RetargetSurface(const unsigned char *old_bits, const DibEng::Bitmap &,
                         const unsigned *colors, unsigned count);
HDC CreateCompatibleDC(HDC = 0);
bool DeleteDC(HDC);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO *, unsigned usage, void **bits,
                         unsigned section = 0, unsigned offset = 0);
// Flat-address providers for the original GDI32 bitmap thunks and GDI.EXE DDB core.
struct BitmapIO {
    virtual bool Read(unsigned address, void *, unsigned bytes) = 0;
    virtual bool Write(unsigned address, const void *, unsigned bytes) = 0;
    virtual bool Probe(unsigned address, bool write) = 0;
    virtual void Error(unsigned code) = 0;
    virtual void Fault(const char *) = 0;
    virtual unsigned ExpectedVersion() const { return 0x400; }
    virtual ~BitmapIO() = default;
};
unsigned BitmapApi(ApiName name, const unsigned *arguments, BitmapIO &);
bool FontInfo(HFONT, unsigned char (&logical)[60]);
unsigned PatternBrushDib(HBRUSH, unsigned char *data, unsigned capacity);
unsigned ColorMode(HDC, unsigned mode);
unsigned CreateColorSpace(const unsigned char *logical);
bool DeleteColorSpace(unsigned);
bool GetLogColorSpace(unsigned, unsigned char *logical);
unsigned GetColorSpace(HDC, const char *default_profile);
unsigned SetColorSpace(HDC, unsigned space, const char *default_profile);
const char *ColorProfile(HDC);
bool SetColorProfile(HDC, const char *);
struct DibLease {
    unsigned guest=0,bytes=0;unsigned char *host=nullptr;void *context=nullptr;
    void (*release)(void *,const DibLease &)=nullptr;
};
struct DibIO:BitmapIO { virtual bool AllocateDib(unsigned bytes,DibLease &)=0; };
bool IsStretchApi(ApiName);
unsigned StretchApi(ApiName,const unsigned *,DibIO &);
bool IsPaletteApi(ApiName);
unsigned PaletteApi(ApiName,const unsigned*,BitmapIO&);
bool HasPalette(unsigned);
unsigned PaletteGetObject(unsigned,unsigned,unsigned,BitmapIO&);
unsigned DefaultPalette();
// How many entries of the system palette the last RealizePalette changed (DX of
// GDI); USER broadcasts WM_PALETTECHANGED when it is not zero.
unsigned LastRealizeChanged();
// GDI.412 IsDCCurrentPalette: the DC holds the foreground palette.
bool IsDCCurrentPalette(HDC);
// UpdateColors: the pixels of the DC's visible surface go from the slots its
// palette had to the slots it has now.
bool UpdateColors(HDC);
bool IsPixelApi(ApiName);
unsigned PixelApi(ApiName,const unsigned*,BitmapIO&);
bool IsArcApi(ApiName);
unsigned ArcApi(ApiName, const unsigned *, BitmapIO &);
bool IsShapeApi(ApiName);
unsigned ShapeApi(ApiName, const unsigned *, BitmapIO &);
bool IsPenApi(ApiName);
unsigned PenApi(ApiName,const unsigned *,BitmapIO &);
bool HasPen(unsigned);
unsigned PenGetObject(unsigned,unsigned,unsigned,BitmapIO &);
unsigned CreatePen(unsigned,int,COLORREF);
bool IsDibApi(ApiName);
unsigned DibApi(ApiName,const unsigned *,DibIO &);
bool HasDibBitmap(unsigned);
unsigned DibBitmapBits(unsigned, unsigned, unsigned, bool, BitmapIO &);
unsigned DibGetObject(unsigned,unsigned,unsigned,BitmapIO &);
HBITMAP CreateResourceBitmap(const unsigned char *,unsigned,BitmapIO &);

bool IsMappingApi(ApiName name);
unsigned MapApi(ApiName name, const unsigned *arguments, BitmapIO &);
bool IsRegionApi(ApiName name);
// Regions built from the original shape generators, painting through a
// region, the remaining logical brushes and the bitmap dimension pair.
bool IsRegionBrushApi(ApiName name);
unsigned RegionBrushApi(ApiName name, const unsigned *arguments, BitmapIO &);
HBRUSH CreateHatchBrush(unsigned style, COLORREF);
bool HatchBrushInfo(HBRUSH, unsigned (&logbrush)[3]);
unsigned RegionApi(ApiName name, const unsigned *arguments, BitmapIO &);
// The path family of GDI.EXE segment 33, the curve stage of segment 32 and the
// two flood fills of segment 19.
bool IsPathApi(ApiName name);
unsigned PathApi(ApiName name, const unsigned *arguments, BitmapIO &);
bool PathRecording(HDC);
// True when the call went into the open path of its device context; `result`
// then carries what the export answers.
bool PathRecord(ApiName name, const unsigned *arguments, BitmapIO &, unsigned &result);
bool CaretBitmap(unsigned,unsigned&,unsigned&);
bool ReferenceCaretBitmap(unsigned,bool);
bool DrawCaret(HDC,unsigned,int,int,int,int);
bool BitmapInfo(HBITMAP, unsigned (&bitmap32)[6]);
bool BitmapDimensions(HBITMAP, int &width, int &height);
bool HasOriginalBitmap(HGDIOBJ);
bool ObjectExists(HGDIOBJ);
bool IsPrivateDefault(HGDIOBJ);
HGDIOBJ FullHandle(HGDIOBJ); // the handle a word names, zero for none
void ShutdownBitmaps(); // Native provider teardown after application DCs are released.
// A process ends. A DIB section it made and another task owns by now
// (SetObjectOwner) outlives it; its bits move out of the memory of the dying
// process into bytes of their own. context is the DibLease context.
void DetachDibLeases(void *context);
HBRUSH CreateSolidBrush(COLORREF);
// GDI.604 SetSolidBrush: recolour a solid brush without changing its handle.
bool SetSolidBrushColour(HBRUSH, COLORREF);
HBRUSH CreateNullBrush();
void ProtectStockBrush(HBRUSH, bool);
bool ProtectedStockBrush(HBRUSH);
bool SolidBrushInfo(HBRUSH, unsigned (&logbrush)[3]);
COLORREF GetPixel(HDC, int x, int y);
HBRUSH CreatePatternBrush(HBITMAP); // Original DDB snapshots; native realization supports 1/32 bpp.
HBRUSH CreateDibPatternBrush(const unsigned char *dib, unsigned size, unsigned usage); // GDI
HFONT CreateFontResource(unsigned index); // native factory: six original MS Sans Serif FNTs
HFONT CreateFontMemory(const unsigned char *, unsigned length);
// A realized TrueType font (src/gdi_truetype.h); the view is not copied.
HFONT CreateFontOutline(const DibText::Font &);
// A font over an FNT image that outlives every handle (the font catalogue of
// src/gdi_font_catalog.cpp); the bytes are not copied.
HFONT CreateFontView(const unsigned char *, unsigned length);
// The font every new device context starts with. Win98 selects the System
// font (SYSTEM.INI [boot] fonts.fon); without one, the first built-in MS Sans
// Serif resource stays the default.
void SetDefaultFont(const unsigned char *, unsigned length);
// A copy of a font whose FNT header carries dfItalic, dfUnderline and
// dfStrikeOut (bytes 80 to 82), the way GDI hands a logical font with those
// attributes to the driver. ExtTextOutA draws underline and strikeout.
HFONT CreateFontVariant(HFONT base, bool italic, bool underline, bool strikeout, bool bold = false);
bool GetTextMetricsA(HDC, TEXTMETRICA *);
DibText::Font SelectedFont(HDC);
// Outline glyph indices, sharing clipping, mapping and alignment with ANSI output.
bool ExtTextOutGlyphs(HDC,int,int,unsigned,const RECT*,const unsigned short*,unsigned,const int*);
bool GetTextExtentGlyphs(HDC,const unsigned short*,int,SIZE*);
bool FontMetrics(const DibText::Font &, TEXTMETRICA *);
bool RectVisible(HDC, const RECT *);                  // GDI32 ordinal 357 / GDI.104
unsigned GetClipBox(HDC, RECT *);                     // GDI32 ordinal 241 / GDI.77
bool PtVisible(HDC, int x, int y);                   // GDI32 ordinal 354 / GDI.103
unsigned GetObjectType(HGDIOBJ);
unsigned IsGdiObject(HGDIOBJ);                       // GDI.462
unsigned SetObjectOwner(HGDIOBJ, unsigned owner);    // GDI32 ordinal 400 / GDI.461
unsigned GetObjectOwner(HGDIOBJ);                    // native owner query
unsigned DeleteObjectsOfOwner(unsigned owner);       // GDI, GDI32!ByeByeGDI
unsigned MakeObjectPrivate(HGDIOBJ, bool);           // GDI.463
bool SysDeleteObject(HGDIOBJ);                       // GDI.605, ignores the private bit
HGDIOBJ GetCurrentObject(HDC,unsigned);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
bool DeleteObject(HGDIOBJ);
int SaveDC(HDC);
bool RestoreDC(HDC, int saved);
bool SetViewportOrgEx(HDC, int x, int y, POINT *previous = nullptr);
bool SetBrushOrgEx(HDC, int x, int y, POINT *previous = nullptr);
bool GetBrushOrgEx(HDC, POINT *);
bool SetDeviceOrigin(HDC, int x, int y, bool local_surface = false);
bool GetDeviceOrigin(HDC, POINT *);
int IntersectClipRect(HDC, int left, int top, int right, int bottom);
int ExcludeClipRect(HDC, int left, int top, int right, int bottom);
int IntersectClipRegion(HDC, const RECT *, unsigned count); // Native disjoint client rectangles.
// Native region providers for USER window regions. Original band storage.
HGDIOBJ CreateRegionRect(int left, int top, int right, int bottom);
int CombineRegions(HGDIOBJ destination, HGDIOBJ first, HGDIOBJ second, unsigned mode);
unsigned RegionRectangles(HGDIOBJ, RECT *out, unsigned capacity, int dx, int dy);
// An owned, complete copy; unlike the count-only API, Read distinguishes
// an empty region from failure to allocate its rectangle snapshot.
struct RegionRectanglesCopy {
    RECT *rectangles=nullptr; unsigned count=0;
    RegionRectanglesCopy()=default;
    RegionRectanglesCopy(const RegionRectanglesCopy&)=delete;
    RegionRectanglesCopy &operator=(const RegionRectanglesCopy&)=delete;
    ~RegionRectanglesCopy();
    bool Read(HGDIOBJ,int dx=0,int dy=0);
};
bool RegionContainsPoint(HGDIOBJ, int x, int y);
bool OffsetRegionObject(HGDIOBJ, int dx, int dy);
bool RegionBounds(HGDIOBJ, RECT *);
// The three GDI answers the shared USER scroll worker needs.
bool MappingExtents(HDC, SIZE &window, SIZE &viewport);
bool DeviceRect(HDC, RECT &);      // LPtoDP over both corners, GDI.99
bool LogicalRect(HDC, RECT &);     // DPtoLP over both corners, GDI.67
HGDIOBJ ClipRegionObject(HDC);     // visible region and clip region, device units
bool CopyApplicationClip(HDC, HGDIOBJ &); // Zero region means no application clip.
bool SelectClipRegion(HDC, HGDIOBJ);
// The colour table of the surface a device context draws on, the table DIBENG
// walks in EnumObj. Zero entries for a surface deeper than eight bits, ~0u for
// a palettized surface without a table in this port.
unsigned SurfaceColours(HDC, unsigned *table, unsigned capacity);
bool CopySurface(HDC, int x, int y, const DibEng::Bitmap &); // Compositor; ignores source DC state.
bool ResetSurfaceDC(HDC); // Native cached-DC lease: default attributes, same bitmap.
bool ResetSystemClip(HDC); // Own/class DC lease: drop the visible clip, keep the rest.
COLORREF GetTextColor(HDC);
COLORREF GetBkColor(HDC);
int GetBkMode(HDC);
COLORREF SetTextColor(HDC, COLORREF);
COLORREF SetBkColor(HDC, COLORREF);
int SetBkMode(HDC, int);
unsigned GetTextAlign(HDC);
bool MapperFlags(HDC, const unsigned *set, unsigned &previous); // GDI DC+A4h
bool AspectFilter(HDC, const int *set, int &x, int &y);          // GDI DC+ACh, DC+AEh
unsigned SetMetaRgn(HDC);                                        // GDI
bool MiterLimit(HDC, const unsigned *set, unsigned &previous);  // GDI DC+74h
unsigned SetBoundsRect(HDC, const RECT *, unsigned flags);       // GDI
unsigned GetBoundsRect(HDC, RECT *, unsigned flags);             // GDI
void AccumulateBounds(HDC, int left, int top, int right, int bottom); // GDI, surface units
void AccumulateLogical(HDC, int left, int top, int right, int bottom);
bool AccumulatesBounds(HDC);
bool CurrentPosition(HDC, POINT &);  // logical, DC position of MoveToEx
unsigned SetTextAlign(HDC, unsigned);
int GetTextCharacterExtra(HDC);
int SetTextCharacterExtra(HDC, int);
bool SetTextJustification(HDC, int extra, int breaks);
bool GetTextExtentPoint32A(HDC, const char *, int length, SIZE *);
bool GetCharWidth32A(HDC, unsigned first, unsigned last, int *widths);
bool BitBlt(HDC destination, int x, int y, int width, int height,
            HDC source, int source_x, int source_y, unsigned rop);
bool PatBlt(HDC, int x, int y, int width, int height, unsigned rop);
bool FillRect(HDC, const RECT *, HBRUSH);
bool FrameRect(HDC, const RECT *, HBRUSH);
bool InvertRect(HDC, const RECT *);
bool TextOutA(HDC, int x, int y, const char *text, int length);
bool ExtTextOutA(HDC, int x, int y, unsigned options, const RECT *,
                 const char *text, unsigned length, const int *advances = nullptr);
unsigned LiveObjects();
// Slots of the object table. LiveObjects against this is what the free part of
// GDI's local heap was under Win98; GDI.609 GdiFreeResources reported it.
unsigned ObjectCapacity();
// During a 16-bit gate, handle lookup may match the low word; see
// src/gdi.cpp.
void NarrowHandles(bool on);
}
